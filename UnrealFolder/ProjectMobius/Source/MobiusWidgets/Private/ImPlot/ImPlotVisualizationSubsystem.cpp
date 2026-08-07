/**
 * ImPlot visualization subsystem implementation.
 */
#include "ImPlot/ImPlotVisualizationSubsystem.h"
#include "Slate/Components/SImPlotOverlay.h"
#include "Slate/Components/SMoveableWindow.h"
#include "Slate/Components/SWindowTitleBarWidget.h"
#include "Core/MobiusWidgetSubsystem.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h" // ClipboardCopy for the chart TSV export (S6)
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"             // FlushRenderingCommands before the capture readback
#include "Slate/WidgetRenderer.h"        // offscreen re-render of the chart for "Copy chart image"
#include "Styling/CoreStyle.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "Brushes/SlateDynamicImageBrush.h"
#include "InputCoreTypes.h"
#include "Layout/Clipping.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElementTypes.h"
#include "Rendering/RenderingCommon.h"
#include "UI/Theme/UIThemeSubsystem.h"
#include "UserConfig/UserProjectSettings.h"
#include "Widgets/SWindow.h"
#include "Widgets/SWidget.h"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "implot.h"
// GImPlot, GetCurrentPlot, ShowPlotContextMenu and ShowAxisContextMenu are all IMPLOT_API but live in the
// internal header. They are what let the plot's right-click menu be rebuilt (S6) without patching ImPlot.
#include "implot_internal.h"
namespace
{
	/**
	 * S6: render the plotted series as TSV for the OS clipboard.
	 *
	 * `bIncludeTime` is passed explicitly rather than inferred from a non-empty XHeader: the axis titles
	 * are optional (HasAxisSettingsForChart can be false), so inferring would silently downgrade the
	 * time+values export to values-only on any chart that never set its axis labels.
	 *
	 * %.10g, not the %.2f / %.0f the hover tooltip uses. The tooltip is rounding for a human reading one
	 * point; an export that rounds is data loss the user cannot see once it is in their spreadsheet.
	 * CRLF and a header row because the destination is Excel on Windows.
	 */
	FString BuildChartTsv(const TArray<FVector2D>& Points, const bool bIncludeTime,
		const FString& XHeader, const FString& YHeader)
	{
		const FString TimeLabel = XHeader.IsEmpty() ? TEXT("Time") : XHeader;
		const FString ValueLabel = YHeader.IsEmpty() ? TEXT("Value") : YHeader;

		FString Out;
		Out.Reserve(Points.Num() * (bIncludeTime ? 32 : 16) + 64);
		Out += bIncludeTime ? FString::Printf(TEXT("%s\t%s"), *TimeLabel, *ValueLabel) : ValueLabel;
		Out += TEXT("\r\n");

		for (const FVector2D& Point : Points)
		{
			Out += bIncludeTime
				? FString::Printf(TEXT("%.10g\t%.10g\r\n"), Point.X, Point.Y)
				: FString::Printf(TEXT("%.10g\r\n"), Point.Y);
		}
		return Out;
	}

	/**
	 * Gamma pairing for the chart capture, as two console levers.
	 *
	 * Getting this wrong is not subtle — an extra linear->sRGB encode turns the near-black chart
	 * background into mid grey and the whole image reads washed out — but which pairing is correct
	 * depends on the RHI, and it cannot be settled by reading code. The defaults are the reasoned answer
	 * (gamma-space output stored raw, so ReadPixels returns exactly the bytes that were on screen); these
	 * exist so the other three combinations can be tried from the console in seconds instead of costing
	 * an editor rebuild each.
	 */
	static TAutoConsoleVariable<int32> CVarChartCopyGammaSpace(
		TEXT("Mobius.ChartCopy.GammaSpace"), 1,
		TEXT("Chart image copy: 1 = Slate3D writes gamma-space values (default), 0 = linear."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarChartCopyLinearTarget(
		TEXT("Mobius.ChartCopy.LinearTarget"), 1,
		TEXT("Chart image copy: 1 = render target forces linear gamma so the hardware does NOT re-encode "
			"on write (default), 0 = sRGB target."),
		ECVF_Default);

	/**
	 * S6: put a captured chart on the OS clipboard as an image.
	 *
	 * UE has no image clipboard — FPlatformApplicationMisc only ever calls SetClipboardData with
	 * CF_UNICODETEXT (WindowsPlatformApplicationMisc.cpp) — so this is raw Win32.
	 *
	 * Two decisions that consumers actually notice:
	 *   * CF_DIB is written OPAQUE. Alpha in a 32-bit CF_DIB is interpreted inconsistently — Word,
	 *     PowerPoint and Paint commonly render a transparent pixel as BLACK — so anything not fully
	 *     opaque is composited onto Backdrop and the alpha byte is forced to 255. A chart that pastes
	 *     as a black rectangle is the classic failure here.
	 *   * biHeight is NEGATIVE, i.e. top-down rows, which matches ReadPixels' order. A positive height
	 *     means bottom-up and pastes the chart upside down.
	 *
	 * FColor is already B,G,R,A in memory on Windows, which is exactly the DIB channel order, so the
	 * rows copy straight across.
	 */
	bool CopyImageToClipboard(const TArray<FColor>& Pixels, const FIntPoint& Size, const FColor Backdrop)
	{
#if PLATFORM_WINDOWS
		if (Size.X <= 0 || Size.Y <= 0 || Pixels.Num() < Size.X * Size.Y)
		{
			return false;
		}

		const SIZE_T HeaderBytes = sizeof(BITMAPINFOHEADER);
		const SIZE_T PixelBytes = static_cast<SIZE_T>(Size.X) * static_cast<SIZE_T>(Size.Y) * 4;

		HGLOBAL GlobalMem = ::GlobalAlloc(GMEM_MOVEABLE, HeaderBytes + PixelBytes);
		if (GlobalMem == nullptr)
		{
			return false;
		}

		void* Locked = ::GlobalLock(GlobalMem);
		if (Locked == nullptr)
		{
			::GlobalFree(GlobalMem);
			return false;
		}

		BITMAPINFOHEADER* Header = static_cast<BITMAPINFOHEADER*>(Locked);
		FMemory::Memzero(Header, HeaderBytes);
		Header->biSize = sizeof(BITMAPINFOHEADER);
		Header->biWidth = Size.X;
		Header->biHeight = -Size.Y; // top-down; see note above
		Header->biPlanes = 1;
		Header->biBitCount = 32;
		Header->biCompression = BI_RGB;
		Header->biSizeImage = static_cast<DWORD>(PixelBytes);

		FColor* Dest = reinterpret_cast<FColor*>(static_cast<uint8*>(Locked) + HeaderBytes);
		for (int32 Index = 0; Index < Size.X * Size.Y; ++Index)
		{
			const FColor Src = Pixels[Index];
			if (Src.A == 255)
			{
				Dest[Index] = FColor(Src.R, Src.G, Src.B, 255);
				continue;
			}
			const int32 Alpha = Src.A;
			const int32 Inv = 255 - Alpha;
			Dest[Index] = FColor(
				static_cast<uint8>((Src.R * Alpha + Backdrop.R * Inv) / 255),
				static_cast<uint8>((Src.G * Alpha + Backdrop.G * Inv) / 255),
				static_cast<uint8>((Src.B * Alpha + Backdrop.B * Inv) / 255),
				255);
		}

		::GlobalUnlock(GlobalMem);

		if (!::OpenClipboard(nullptr))
		{
			::GlobalFree(GlobalMem);
			return false;
		}
		::EmptyClipboard();
		const bool bSet = ::SetClipboardData(CF_DIB, GlobalMem) != nullptr;
		::CloseClipboard();
		if (!bSet)
		{
			// Ownership only transfers on success; freeing after a successful set would corrupt it.
			::GlobalFree(GlobalMem);
		}
		return bSet;
#else
		return false;
#endif
	}

	/**
	 * R1: paint the ImGui/ImPlot colour table from the Mobius palette.
	 *
	 * ImGui::StyleColorsDark() / ImPlot::StyleColorsDark() are the STOCK upstream themes, not ours, and
	 * ImPlot's dark PlotBg is literally ImVec4(0, 0, 0, 0.50) (implot.cpp) — which is why the chart's
	 * plotting area rendered near-black while the window chrome around it followed the theme. Nothing
	 * here was ever palette-driven; the stock call is the whole story.
	 *
	 * Called per frame, right after the stock call seeds every remaining entry — same idiom already used
	 * for the stock call itself, and it means a freshly created context and a live theme toggle both land
	 * without any change tracking.
	 *
	 * The palette is authored in LINEAR space (Slate takes FLinearColor), but ImGui's colour table is
	 * consumed as sRGB-ish floats — the stock 0.06 dark window background reads as #101010 on screen, not
	 * as linear 0.06. So every value is sRGB-encoded on the way in, or the whole chart would come out
	 * far too dark.
	 */
	void ApplyMobiusPaletteToImGui(const bool bLight)
	{
		auto Role = [bLight](const EMobiusPaletteRole InRole, const float Alpha = 1.0f) -> ImVec4
		{
			const FColor Srgb = MobiusThemePalette::Color(InRole, bLight).ToFColor(/*bSRGB=*/true);
			return ImVec4(Srgb.R / 255.0f, Srgb.G / 255.0f, Srgb.B / 255.0f, Alpha);
		};

		ImVec4* Colors = ImGui::GetStyle().Colors;
		Colors[ImGuiCol_WindowBg]      = Role(EMobiusPaletteRole::RibbonBg);
		Colors[ImGuiCol_ChildBg]       = Role(EMobiusPaletteRole::RibbonBg);
		Colors[ImGuiCol_PopupBg]       = Role(EMobiusPaletteRole::RibbonBg);
		Colors[ImGuiCol_Border]        = Role(EMobiusPaletteRole::PanelHeaderBorder);
		Colors[ImGuiCol_Text]          = Role(EMobiusPaletteRole::LabelText);
		Colors[ImGuiCol_TextDisabled]  = Role(EMobiusPaletteRole::MicroText);
		Colors[ImGuiCol_FrameBg]       = Role(EMobiusPaletteRole::InputBg);
		Colors[ImGuiCol_TitleBg]       = Role(EMobiusPaletteRole::TitlebarBg);
		Colors[ImGuiCol_TitleBgActive] = Role(EMobiusPaletteRole::TitlebarBg);

		// Interactive roles. Until the copy controls landed nothing in this overlay was clickable, so these
		// were never mapped and every button, menu highlight and tick mark still came from ImGui's STOCK
		// dark/light theme — a blue-grey that belongs to no Mobius palette row. Buttons take the same three
		// roles the Slate buttons do, so an ImGui button and a UMG one read as the same control.
		Colors[ImGuiCol_Button]         = Role(EMobiusPaletteRole::ButtonBg);
		Colors[ImGuiCol_ButtonHovered]  = Role(EMobiusPaletteRole::ButtonHoverBg);
		Colors[ImGuiCol_ButtonActive]   = Role(EMobiusPaletteRole::ButtonPressedBg);
		Colors[ImGuiCol_FrameBgHovered] = Role(EMobiusPaletteRole::ButtonHoverBg);
		Colors[ImGuiCol_FrameBgActive]  = Role(EMobiusPaletteRole::ButtonPressedBg);
		// Header* is what MenuItem / Selectable highlight with, i.e. every row of the right-click menu.
		Colors[ImGuiCol_Header]         = Role(EMobiusPaletteRole::HoverBg);
		Colors[ImGuiCol_HeaderHovered]  = Role(EMobiusPaletteRole::ButtonHoverBg);
		Colors[ImGuiCol_HeaderActive]   = Role(EMobiusPaletteRole::ButtonPressedBg);
		Colors[ImGuiCol_CheckMark]      = Role(EMobiusPaletteRole::Accent);
		Colors[ImGuiCol_Separator]      = Role(EMobiusPaletteRole::PanelDivider);
		Colors[ImGuiCol_MenuBarBg]      = Role(EMobiusPaletteRole::RibbonBg);

		ImVec4* Plot = ImPlot::GetStyle().Colors;
		// PlotBg is the card the series are drawn on — same role the migrated card backgrounds use.
		Plot[ImPlotCol_PlotBg]       = Role(EMobiusPaletteRole::InputBg);
		Plot[ImPlotCol_PlotBorder]   = Role(EMobiusPaletteRole::InputBorder);
		Plot[ImPlotCol_FrameBg]      = Role(EMobiusPaletteRole::RibbonBg);
		Plot[ImPlotCol_LegendBg]     = Role(EMobiusPaletteRole::RibbonBg);
		Plot[ImPlotCol_LegendBorder] = Role(EMobiusPaletteRole::PanelHeaderBorder);
		Plot[ImPlotCol_LegendText]   = Role(EMobiusPaletteRole::LabelText);
		Plot[ImPlotCol_TitleText]    = Role(EMobiusPaletteRole::LabelText);
		Plot[ImPlotCol_InlayText]    = Role(EMobiusPaletteRole::LabelText);
		Plot[ImPlotCol_AxisText]     = Role(EMobiusPaletteRole::SublabelText);
		// Grid and ticks stay deliberately faint so they sit under the series, not next to them.
		Plot[ImPlotCol_AxisGrid]     = Role(EMobiusPaletteRole::PanelDivider, 0.6f);
		Plot[ImPlotCol_AxisTick]     = Role(EMobiusPaletteRole::PanelDivider);
	}

	const FName VisualizationDefaultChartId = NAME_None;
}

void UImPlotVisualizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UImPlotVisualizationSubsystem::Deinitialize()
{
        // Unbind the live-theme handler. Dynamic delegate: remove by (object, UFUNCTION name). The theme
        // subsystem outlives this world subsystem, so leaving it bound would retain a stale ref to a
        // torn-down world subsystem across PIE stop / level change.
        if (BoundThemeSubsystem.IsValid())
        {
                BoundThemeSubsystem->OnThemeChanged.RemoveDynamic(this, &UImPlotVisualizationSubsystem::HandleThemeChanged);
        }
        BoundThemeSubsystem.Reset();

        for (auto& Pair : OverlayStates)
        {
                CloseOverlayWindow(Pair.Value);
                Pair.Value.OverlayWidget.Reset();
        }
        OverlayStates.Empty();
        SharedFontAtlas = nullptr;
        SharedFontBrush.Reset();
        SharedFontTextureId = 0;
        SharedFontTextureName = NAME_None;

        Super::Deinitialize();
}

void UImPlotVisualizationSubsystem::ShowOverlay(bool bShow)
{
	ShowOverlayForChart(VisualizationDefaultChartId, bShow);
}

void UImPlotVisualizationSubsystem::ToggleOverlay()
{
	ToggleOverlayForChart(VisualizationDefaultChartId);
}

void UImPlotVisualizationSubsystem::CloseOverlay()
{
	CloseOverlayForChart(VisualizationDefaultChartId);
}

void UImPlotVisualizationSubsystem::SetChartTitle(const FText& InTitle)
{
	SetChartTitleForChart(VisualizationDefaultChartId, InTitle);
}

void UImPlotVisualizationSubsystem::SetAxisSettings(const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax)
{
	SetAxisSettingsForChart(VisualizationDefaultChartId, InXTitle, InYTitle, InXMin, InXMax, InYMin, InYMax);
}

void UImPlotVisualizationSubsystem::SetPlotPoints(const TArray<FVector2D>& InPoints)
{
	SetPlotPointsForChart(VisualizationDefaultChartId, InPoints);
}

void UImPlotVisualizationSubsystem::UpdateLiveSample(double InTimeSeconds, double InCount)
{
	UpdateLiveSampleForChart(VisualizationDefaultChartId, InTimeSeconds, InCount);
}

void UImPlotVisualizationSubsystem::ShowOverlayForChart(const FName& ChartId, bool bShow)
{
        FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
        State.bOverlayVisible = bShow;

        if (bShow)
        {
                State.bWindowOpen = true;
                EnsureOverlayWidget(State, ChartId);
                OpenOverlayWindow(State, ChartId);
        }
        else
        {
                CloseOverlayWindow(State);
        }

        InvalidateOverlay(ChartId);
}

void UImPlotVisualizationSubsystem::ToggleOverlayForChart(const FName& ChartId)
{
	FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
	ShowOverlayForChart(ChartId, !State.bOverlayVisible);
}

void UImPlotVisualizationSubsystem::CloseOverlayForChart(const FName& ChartId)
{
	ShowOverlayForChart(ChartId, false);
}

void UImPlotVisualizationSubsystem::SetChartTitleForChart(const FName& ChartId, const FText& InTitle)
{
	FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
	State.ChartTitle = InTitle;
	InvalidateOverlay(ChartId);
}

void UImPlotVisualizationSubsystem::SetAxisSettingsForChart(const FName& ChartId, const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax)
{
	FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
	State.XAxisTitle = InXTitle;
	State.YAxisTitle = InYTitle;
	State.XMin = InXMin;
	State.XMax = InXMax;
	State.YMin = InYMin;
	State.YMax = InYMax;
	State.bHasAxisSettings = true;
	InvalidateOverlay(ChartId);
}

void UImPlotVisualizationSubsystem::SetPlotPointsForChart(const FName& ChartId, const TArray<FVector2D>& InPoints)
{
	FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
	State.PlotPoints = InPoints;
	InvalidateOverlay(ChartId);
}

void UImPlotVisualizationSubsystem::UpdateLiveSampleForChart(const FName& ChartId, double InTimeSeconds, double InCount)
{
	FImPlotOverlayState& State = GetOrCreateOverlayState(ChartId);
	if (State.bHasLiveSample)
	{
		State.LiveSampleThickness = FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(InTimeSeconds - State.LiveTimeSeconds));
		State.bHasLiveSampleThickness = true;
	}
	else
	{
		State.LiveSampleThickness = 0.0;
		State.bHasLiveSampleThickness = false;
	}

	State.LiveTimeSeconds = InTimeSeconds;
	State.LiveCount = InCount;
	State.bHasLiveSample = true;
	InvalidateOverlay(ChartId);
}

bool UImPlotVisualizationSubsystem::IsOverlayVisible() const
{
	return IsOverlayVisibleForChart(VisualizationDefaultChartId);
}

const FText& UImPlotVisualizationSubsystem::GetChartTitle() const
{
	return GetChartTitleForChart(VisualizationDefaultChartId);
}

const FText& UImPlotVisualizationSubsystem::GetXAxisTitle() const
{
	return GetXAxisTitleForChart(VisualizationDefaultChartId);
}

const FText& UImPlotVisualizationSubsystem::GetYAxisTitle() const
{
	return GetYAxisTitleForChart(VisualizationDefaultChartId);
}

void UImPlotVisualizationSubsystem::GetAxisLimits(double& OutXMin, double& OutXMax, double& OutYMin, double& OutYMax) const
{
	GetAxisLimitsForChart(VisualizationDefaultChartId, OutXMin, OutXMax, OutYMin, OutYMax);
}

bool UImPlotVisualizationSubsystem::HasAxisSettings() const
{
	return HasAxisSettingsForChart(VisualizationDefaultChartId);
}

const TArray<FVector2D>& UImPlotVisualizationSubsystem::GetPlotPoints() const
{
	return GetPlotPointsForChart(VisualizationDefaultChartId);
}

bool UImPlotVisualizationSubsystem::HasLiveSample() const
{
	return HasLiveSampleForChart(VisualizationDefaultChartId);
}

void UImPlotVisualizationSubsystem::GetLiveSample(double& OutTimeSeconds, double& OutCount) const
{
	GetLiveSampleForChart(VisualizationDefaultChartId, OutTimeSeconds, OutCount);
}

bool UImPlotVisualizationSubsystem::HasLiveSampleThickness() const
{
	return HasLiveSampleThicknessForChart(VisualizationDefaultChartId);
}

double UImPlotVisualizationSubsystem::GetLiveSampleThickness() const
{
	return GetLiveSampleThicknessForChart(VisualizationDefaultChartId);
}

bool UImPlotVisualizationSubsystem::IsOverlayVisibleForChart(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->bOverlayVisible : false;
}

const FText& UImPlotVisualizationSubsystem::GetChartTitleForChart(const FName& ChartId) const
{
	static const FText EmptyText = FText::GetEmpty();
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->ChartTitle : EmptyText;
}

const FText& UImPlotVisualizationSubsystem::GetXAxisTitleForChart(const FName& ChartId) const
{
	static const FText EmptyText = FText::GetEmpty();
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->XAxisTitle : EmptyText;
}

const FText& UImPlotVisualizationSubsystem::GetYAxisTitleForChart(const FName& ChartId) const
{
	static const FText EmptyText = FText::GetEmpty();
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->YAxisTitle : EmptyText;
}

void UImPlotVisualizationSubsystem::GetAxisLimitsForChart(const FName& ChartId, double& OutXMin, double& OutXMax, double& OutYMin, double& OutYMax) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	if (State)
	{
		OutXMin = State->XMin;
		OutXMax = State->XMax;
		OutYMin = State->YMin;
		OutYMax = State->YMax;
		return;
	}
	OutXMin = 0.0;
	OutXMax = 1.0;
	OutYMin = 0.0;
	OutYMax = 1.0;
}

bool UImPlotVisualizationSubsystem::HasAxisSettingsForChart(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->bHasAxisSettings : false;
}

const TArray<FVector2D>& UImPlotVisualizationSubsystem::GetPlotPointsForChart(const FName& ChartId) const
{
	static const TArray<FVector2D> EmptyPoints;
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->PlotPoints : EmptyPoints;
}

bool UImPlotVisualizationSubsystem::HasLiveSampleForChart(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->bHasLiveSample : false;
}

void UImPlotVisualizationSubsystem::GetLiveSampleForChart(const FName& ChartId, double& OutTimeSeconds, double& OutCount) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	if (State)
	{
		OutTimeSeconds = State->LiveTimeSeconds;
		OutCount = State->LiveCount;
		return;
	}
	OutTimeSeconds = 0.0;
	OutCount = 0.0;
}

bool UImPlotVisualizationSubsystem::HasLiveSampleThicknessForChart(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->bHasLiveSampleThickness : false;
}

double UImPlotVisualizationSubsystem::GetLiveSampleThicknessForChart(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	return State ? State->LiveSampleThickness : 0.0;
}

UImPlotVisualizationSubsystem::FImPlotOverlayState& UImPlotVisualizationSubsystem::GetOrCreateOverlayState(const FName& ChartId)
{
	return OverlayStates.FindOrAdd(ChartId);
}

UImPlotVisualizationSubsystem::FImPlotOverlayState* UImPlotVisualizationSubsystem::FindOverlayState(const FName& ChartId)
{
	return OverlayStates.Find(ChartId);
}

const UImPlotVisualizationSubsystem::FImPlotOverlayState* UImPlotVisualizationSubsystem::FindOverlayState(const FName& ChartId) const
{
	return OverlayStates.Find(ChartId);
}

void UImPlotVisualizationSubsystem::EnsureOverlayWidget(FImPlotOverlayState& State, const FName& ChartId)
{
	if (!State.OverlayWidget.IsValid())
	{
		State.OverlayWidget = SNew(SImPlotOverlay)
			.Subsystem(this)
			.ChartId(ChartId);
	}
}

void UImPlotVisualizationSubsystem::OpenOverlayWindow(FImPlotOverlayState& State, const FName& ChartId)
{
	if (!State.OverlayWidget.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	// Bind the live-theme handler once, now that a GameInstance (and its theme subsystem) is available.
	EnsureThemeChangeBinding();

	if (!State.OverlayWindow.IsValid())
	{
		const FText WindowTitle = FText::FromString(TEXT("UE Plot Overlay"));

		// D8/Q3: themed window chrome held at a stable subsystem address (SWindow keeps the style by
		// pointer). The SWindowTitleBarWidget also polls the theme per-paint so the title bar follows a
		// live toggle; the border/background here theme at open time, and HandleThemeChanged re-themes it
		// in place on a live toggle. Seed the CoreStyle default only once - if the theme lookup fails on a
		// later open, we keep the last good themed style instead of flashing back to the CoreStyle gray.
		if (!bChartWindowStyleInitialized)
		{
			ChartWindowStyle = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
			bChartWindowStyleInitialized = true;
		}
		if (const UWorld* World = GetWorld())
		{
			if (const UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (const UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
				{
					ChartWindowStyle = Theme->GetThemedWindowStyle();
				}
			}
		}

		SAssignNew(State.OverlayWindow, SMoveableWindow)
			.Title(WindowTitle)
			.Style(&ChartWindowStyle)
			.SizingRule(ESizingRule::UserSized)
			.FocusWhenFirstShown(false)
			.ActivationPolicy(EWindowActivationPolicy::Never)
			.SupportsMaximize(true)
			.SupportsMinimize(true)
			.IsTopmostWindow(true)
			.HasCloseButton(true)
			.AutoCenter(EAutoCenter::PreferredWorkArea)
			.ClientSize(FVector2D(640.0f, 420.0f))
			.WindowPanelContent(State.OverlayWidget);

		// Don't Set Content on windows as this sets the content for titlebar, and the window area
		//State.OverlayWindow->SetContent(State.OverlayWidget.ToSharedRef());
		State.OverlayWindow->SetOnWindowClosed(FOnWindowClosed::CreateUObject(this, &UImPlotVisualizationSubsystem::HandleWindowClosed, ChartId));

		FSlateApplication::Get().AddWindow(State.OverlayWindow.ToSharedRef());
		RegisterMoveableWindowActivity(State);
	}
	else
	{
		State.OverlayWindow->BringToFront(true);
	}
}

void UImPlotVisualizationSubsystem::EnsureThemeChangeBinding()
{
	if (BoundThemeSubsystem.IsValid())
	{
		// Already bound (weak ptr set by a previous successful resolve).
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
			{
				// AddUniqueDynamic guards against a duplicate bind; the weak ptr is what Deinitialize
				// needs to RemoveDynamic (dynamic delegates have no FDelegateHandle).
				Theme->OnThemeChanged.AddUniqueDynamic(this, &UImPlotVisualizationSubsystem::HandleThemeChanged);
				BoundThemeSubsystem = Theme;
			}
		}
	}
}

void UImPlotVisualizationSubsystem::HandleThemeChanged()
{
	// Re-theme the shared window chrome IN PLACE: SWindow holds &ChartWindowStyle by pointer, so we must
	// assign into the existing member, never reassign a new object. GetThemedWindowStyle() reads the
	// theme subsystem's CurrentTheme, which is already the new value while OnThemeChanged fires.
	if (UUIThemeSubsystem* Theme = BoundThemeSubsystem.Get())
	{
		ChartWindowStyle = Theme->GetThemedWindowStyle();
		bChartWindowStyleInitialized = true;
	}

	// Force a repaint so the open chart window(s) pick up the retinted border/background brushes.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(false);
	}
}

void UImPlotVisualizationSubsystem::CloseOverlayWindow(FImPlotOverlayState& State)
{
        UnregisterMoveableWindowActivity(State);
        State.bWindowOpen = false;
        if (!State.OverlayWindow.IsValid() || !FSlateApplication::IsInitialized())
        {
                State.OverlayWindow.Reset();
                DestroyOverlayContext(State);
                return;
        }
        // Spec §5: play the close animation (reverse fade + sink) then self-destroy — the window is not
        // dropped mid-anim. The plot area blanks during the fade (context torn down below, OnPaint guards).
        State.OverlayWindow->PlayCloseAnimationThenDestroy();
        State.OverlayWindow.Reset();
        DestroyOverlayContext(State);
}

void UImPlotVisualizationSubsystem::HandleWindowClosed(const TSharedRef<SWindow>& ClosedWindow, FName ChartId)
{
        FImPlotOverlayState* State = FindOverlayState(ChartId);
        if (!State)
        {
                return;
        }

        if (State->OverlayWindow == ClosedWindow)
        {
                State->OverlayWindow.Reset();
                State->bOverlayVisible = false;
                State->bWindowOpen = false;
                UnregisterMoveableWindowActivity(*State);
                DestroyOverlayContext(*State);
        }
}

void UImPlotVisualizationSubsystem::ServicePendingImageCopy(const FName& ChartId)
{
        FImPlotOverlayState* State = FindOverlayState(ChartId);
        if (State == nullptr || !State->bImageCopyRequested || !State->OverlayWidget.IsValid())
        {
                return;
        }

        // Cleared BEFORE the render: DrawWidget repaints this same widget, and a request left standing
        // would be re-serviced on the next tick forever.
        State->bImageCopyRequested = false;

        const TSharedRef<SWidget> OverlayRef = State->OverlayWidget.ToSharedRef();
        const FVector2D DrawSize(OverlayRef->GetTickSpaceGeometry().GetLocalSize());
        if (DrawSize.X < 1.0 || DrawSize.Y < 1.0)
        {
                return;
        }

        const bool bLinearTarget = CVarChartCopyLinearTarget.GetValueOnGameThread() != 0;
        const bool bGammaSpace = CVarChartCopyGammaSpace.GetValueOnGameThread() != 0;

        const FIntPoint TargetSize(FMath::CeilToInt(DrawSize.X), FMath::CeilToInt(DrawSize.Y));
        if (CaptureRenderTarget == nullptr
                || CaptureRenderTarget->SizeX != TargetSize.X
                || CaptureRenderTarget->SizeY != TargetSize.Y
                || bCaptureTargetLinearGamma != bLinearTarget) // rebuild when the console lever moves
        {
                // Built by hand rather than with FWidgetRenderer::CreateTargetFor, because that helper
                // derives BOTH of its gamma flags from one argument and the pairing it produces
                // double-corrects — which is what made the first capture come out washed out (a near-black
                // chart background landed at mid grey, the signature of an extra linear->sRGB encode).
                //
                // CreateTargetFor(true) gives SRGB=false but bForceLinearGamma=FALSE, so the RHI texture is
                // created sRGB and the hardware encodes on write — while the Slate3D renderer, constructed
                // with bUseGammaCorrection=true below, is ALREADY writing gamma-space values. Encoded twice.
                //
                // What is wanted is gamma-space output stored raw: force linear gamma on the resource so
                // nothing re-encodes, and ReadPixels then hands back exactly the bytes that were on screen.
                // 8-bit is safe here — Slate's recommended colour format is PF_B8G8R8A8, so there is no
                // float path to lose precision through.
                CaptureRenderTarget = NewObject<UTextureRenderTarget2D>(this);
                CaptureRenderTarget->Filter = TF_Bilinear;
                CaptureRenderTarget->ClearColor = FLinearColor::Transparent;
                CaptureRenderTarget->SRGB = !bLinearTarget;
                CaptureRenderTarget->TargetGamma = 1.0f;
                CaptureRenderTarget->InitCustomFormat(TargetSize.X, TargetSize.Y, PF_B8G8R8A8, bLinearTarget);
                CaptureRenderTarget->UpdateResourceImmediate(true);
                bCaptureTargetLinearGamma = bLinearTarget;
        }
        if (CaptureRenderTarget == nullptr)
        {
                return;
        }

        {
                // Scoped so the flag is down again before anything else can paint, even on an early return.
                TGuardValue<bool> CaptureGuard(bCapturingForImageCopy, true);
                FWidgetRenderer Renderer(bGammaSpace);
                Renderer.SetIsPrepassNeeded(true);
                Renderer.DrawWidget(CaptureRenderTarget, OverlayRef, DrawSize, /*DeltaTime*/0.0f);
                // DrawWidget only ENQUEUES; without this the readback races an empty target.
                FlushRenderingCommands();
        }

        FTextureRenderTargetResource* Resource = CaptureRenderTarget->GameThread_GetRenderTargetResource();
        TArray<FColor> Pixels;
        if (Resource == nullptr || !Resource->ReadPixels(Pixels))
        {
                return;
        }

        // Backdrop for any non-opaque pixel. ImGui fills its window with WindowBg so most of the image is
        // already opaque, but the corners it rounds off are not, and those are what paste black.
        const UUserProjectSettings* UserSettings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);
        const bool bLight = !UserSettings || UserSettings->GetUseLightUITheme();
        const FColor Backdrop = MobiusThemePalette::Color(EMobiusPaletteRole::WellBg, bLight).ToFColor(/*bSRGB*/true);

        CopyImageToClipboard(Pixels, TargetSize, Backdrop);
}

void UImPlotVisualizationSubsystem::InvalidateOverlay(const FName& ChartId) const
{
	const FImPlotOverlayState* State = FindOverlayState(ChartId);
	if (State && State->OverlayWidget.IsValid())
	{
		State->OverlayWidget->Invalidate(EInvalidateWidget::Paint);
	}
}

void UImPlotVisualizationSubsystem::RegisterMoveableWindowActivity(FImPlotOverlayState& State)
{
	if (State.bMoveableWindowActivityRegistered)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UMobiusWidgetSubsystem* WidgetSubsystem = World->GetSubsystem<UMobiusWidgetSubsystem>())
		{
			State.MoveableWindowSubsystem = WidgetSubsystem;
			WidgetSubsystem->RegisterMoveableWindowActivity();
			State.bMoveableWindowActivityRegistered = true;
		}
	}
}

void UImPlotVisualizationSubsystem::UnregisterMoveableWindowActivity(FImPlotOverlayState& State)
{
	if (!State.bMoveableWindowActivityRegistered)
	{
		return;
	}

	if (State.MoveableWindowSubsystem.IsValid())
	{
		State.MoveableWindowSubsystem->UnregisterMoveableWindowActivity();
		State.MoveableWindowSubsystem.Reset();
	}
	else if (UWorld* World = GetWorld())
	{
		if (UMobiusWidgetSubsystem* WidgetSubsystem = World->GetSubsystem<UMobiusWidgetSubsystem>())
		{
			WidgetSubsystem->UnregisterMoveableWindowActivity();
		}
	}

	State.bMoveableWindowActivityRegistered = false;
}


int32 UImPlotVisualizationSubsystem::PaintOverlayForChart(const FName& ChartId, const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
        const FWidgetStyle& InWidgetStyle, bool bParentEnabled, const TSharedRef<const SWidget>& Widget,
        const FSimpleDelegate& OnRequestClose)
{
        FImPlotOverlayState* State = FindOverlayState(ChartId);
        if (!State || !State->bOverlayVisible)
        {
                return LayerId;
        }

        EnsureOverlayContext(*State);
        if (!State->ImGuiContext || !State->ImPlotContext)
        {
                return LayerId;
        }

        ImGui::SetCurrentContext(State->ImGuiContext);
        ImPlot::SetCurrentContext(State->ImPlotContext);

        // Match the Mobius UI theme (light = design 4b, dark = 7a). Applied per frame: it is a
        // trivial colour-table fill, covers freshly created contexts, and follows a runtime theme
        // toggle without any change tracking. FontScaleMain is set again after this each frame.
        {
                const UUserProjectSettings* UserSettings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);
                const bool bLight = !UserSettings || UserSettings->GetUseLightUITheme();
                if (bLight)
                {
                        ImGui::StyleColorsLight();
                        ImPlot::StyleColorsLight();
                }
                else
                {
                        ImGui::StyleColorsDark();
                        ImPlot::StyleColorsDark();
                }

                // The stock call above seeds the whole table; this overwrites the entries the Mobius
                // palette owns. Order matters — the stock call resets every colour, so it has to run first.
                ApplyMobiusPaletteToImGui(bLight);
        }

        ImGuiIO& IO = ImGui::GetIO();
        // Calculate display size - will be used for both IO.DisplaySize and ImGui window size
        // Size from THIS WIDGET, never from the window.
        //
        // DisplaySize used to come from Window->GetClientRectInScreen(), which is wrong here:
        // OpenOverlayWindow passes the overlay as `WindowPanelContent`, so it sits BELOW SMoveableWindow's
        // title bar and the client rect is taller than the widget. ImGui laid the chart out into a space
        // bigger than it was drawn into. RenderDrawData already positions its output from
        // AllottedGeometry's layout transform, so the widget's own local size is the frame of reference
        // that agrees with what ends up on screen.
        FVector2f DisplaySize = FVector2f(AllottedGeometry.GetLocalSize());
        // Replay the last on-screen DPI during a capture rather than defaulting to 1.0, or the shared
        // glyph atlas re-bakes for the capture and re-bakes back on the next real paint — across every
        // chart, since the atlas is shared.
        float WindowDpiScale = bCapturingForImageCopy ? State->LastPaintDpiScale : 1.0f;
        if (FSlateApplication::IsInitialized())
        {
                // The cursor is tracked by SImPlotOverlay's own pointer events and arrives ALREADY in
                // local space. It is not derived here, because Slate has more than one absolute space and
                // this function cannot see which one it is in: the FGeometry passed to OnPaint is in
                // WINDOW space, while FSlateApplication::GetCursorPos() is DESKTOP. Combining them leaves
                // an error equal to the window's screen position — which looks correct only while the
                // window sits in the top-left corner of the display.
                if (State->OverlayWidget.IsValid())
                {
                        const FVector2D LocalCursorPos = State->OverlayWidget->GetLocalCursorPosition();
                        IO.MousePos = ImVec2(static_cast<float>(LocalCursorPos.X), static_cast<float>(LocalCursorPos.Y));
                }

                // The window is still the only place a DPI scale can come from; it just must not decide
                // cursor position or display size.
                if (!bCapturingForImageCopy)
                {
                        if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(Widget))
                        {
                                WindowDpiScale = Window->GetDPIScaleFactor();
                        }
                }

                IO.MouseDown[0] = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
                IO.MouseDown[1] = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::RightMouseButton);
                IO.MouseDown[2] = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::MiddleMouseButton);
        }
        if (!bCapturingForImageCopy)
        {
                State->LastPaintDpiScale = WindowDpiScale;
        }

        // (Re)bake the shared glyph atlas at this window's DPI scale. Must happen BEFORE NewFrame —
        // the atlas is shared across all chart contexts and cannot change mid-frame. Mirrors the
        // release logic in DestroyOverlayContext.
        if (SharedFontBrush.IsValid() && !FMath::IsNearlyEqual(WindowDpiScale, SharedFontAtlasDpiScale, 0.05f))
        {
                SharedFontBrush.Reset();
                SharedFontTextureId = 0;
                SharedFontTextureName = NAME_None; // new name per bake — dynamic brush textures are keyed by name
                SharedFontAtlas->Clear();
        }
        EnsureSharedFontAtlas(WindowDpiScale);

        // Glyphs are baked at 13px * atlas scale; draw them at 13 logical units so layout metrics stay
        // unchanged — the vertex upscale in RenderDrawData maps them 1:1 to physical pixels. Do NOT
        // touch FontScaleDpi/CurrentDpiScale: the logical-units-in / vertex-upscale-out pipeline would
        // double-scale.
        ImGui::GetStyle().FontScaleMain = 1.0f / SharedFontAtlasDpiScale;

        // Use the same DisplaySize for both IO and ImGui window
        IO.DisplaySize = ImVec2(DisplaySize.X, DisplaySize.Y);
        IO.DeltaTime = FMath::Max(1.0e-6f, static_cast<float>(FApp::GetDeltaTime()));

        ImGui::NewFrame();

        // Use DisplaySize (same as IO.DisplaySize) for consistent sizing
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(DisplaySize.X, DisplaySize.Y), ImGuiCond_Always);
        bool bOpen = State->bWindowOpen;
        ImGui::Begin("UE Plot Overlay", &bOpen,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);

        // S6: copy the chart, either as an image or as TSV.
        //
        // FPlatformApplicationMisc::ClipboardCopy, NOT ImGui::SetClipboardText: ImGui's clipboard is scoped
        // to its own context and never reaches the Windows clipboard.
        //
        // Deliberately the ForChart accessor: this function is per-chart, and the legacy GetPlotPoints()
        // reads a different series while looking entirely plausible.
        const TArray<FVector2D>& CopyPoints = GetPlotPointsForChart(ChartId);
        auto CopySeriesToClipboard = [this, &ChartId, &CopyPoints](const bool bIncludeTime)
        {
                // Built here, inside the click handler, never per frame — this is a paint path.
                const FString Tsv = BuildChartTsv(CopyPoints, bIncludeTime,
                        bIncludeTime ? GetXAxisTitleForChart(ChartId).ToString() : FString(),
                        GetYAxisTitleForChart(ChartId).ToString());
                FPlatformApplicationMisc::ClipboardCopy(*Tsv);
        };

        // The copy ACTIONS live only here, on buttons. They used to be duplicated as items in the
        // right-click menu, which read wrong: everything else in that menu is a setting, so two verbs sat
        // among a list of adjustments. The menu now carries only the "Copy Settings" submenu.
        auto DrawCopyButtons = [this, &State, &CopyPoints, &CopySeriesToClipboard](const bool bStacked)
        {
                if (ImGui::SmallButton("Copy chart"))
                {
                        // Only a flag. The capture has to flush rendering commands and re-render this very
                        // widget, neither of which is safe from inside a Slate paint — see
                        // ServicePendingImageCopy.
                        State->bImageCopyRequested = true;
                }
                ImGui::SetItemTooltip("Copy the chart to the clipboard as an image");

                if (!bStacked)
                {
                        ImGui::SameLine();
                }
                ImGui::BeginDisabled(CopyPoints.Num() == 0);
                // ONE data button. Whether it carries the time column is the "Copy with timeline" toggle
                // in Copy Settings, not a second button — two near-identical buttons made the caller
                // choose on every copy about a column that is usually the least interesting one.
                if (ImGui::SmallButton("Copy values"))
                {
                        CopySeriesToClipboard(State->bCopyWithTimeline);
                }
                ImGui::SetItemTooltip(State->bCopyWithTimeline
                                ? "Copy %d row%s of time and value, tab separated"
                                : "Copy %d value%s to the clipboard, one per line",
                        CopyPoints.Num(), CopyPoints.Num() == 1 ? "" : "s");
                ImGui::EndDisabled();
        };

        // Skipped entirely while capturing — the chrome must not appear in the copied image.
        const bool bShowButtons = !bCapturingForImageCopy;
        const EMobiusCopyButtonLocation ButtonLocation = State->CopyButtonLocation;

        if (bShowButtons && ButtonLocation == EMobiusCopyButtonLocation::Top)
        {
                // Above the TITLE, not just above the plot: the title belongs to the chart, so the controls
                // that act on the whole chart sit outside it.
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                DrawCopyButtons(/*bStacked*/false);
        }

        const FString TitleString = GetChartTitleForChart(ChartId).ToString();
        if (!TitleString.IsEmpty())
        {
                // Nudge the title down so it isn't jammed against the window's top border.
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::TextUnformatted(TCHAR_TO_UTF8(*TitleString));
                ImGui::Spacing();
        }

        // Left/Right put the buttons in a column beside the plot, so the plot has to give up that width.
        // Measured from the widest label rather than hard-coded, or a font or DPI change clips it.
        float SideColumnWidth = 0.0f;
        if (bShowButtons && (ButtonLocation == EMobiusCopyButtonLocation::Left
                || ButtonLocation == EMobiusCopyButtonLocation::Right))
        {
                SideColumnWidth = ImGui::CalcTextSize("Copy values").x
                        + ImGui::GetStyle().FramePadding.x * 2.0f
                        + ImGui::GetStyle().ItemSpacing.x;
        }

        if (bShowButtons && ButtonLocation == EMobiusCopyButtonLocation::Left)
        {
                ImGui::BeginGroup();
                DrawCopyButtons(/*bStacked*/true);
                ImGui::EndGroup();
                ImGui::SameLine();
        }

        // ImPlotFlags_NoMenus: ImPlot 0.17 exposes no hook for appending to its context menu, so this
        // suppresses ImPlot's popups and the block before EndPlot below rebuilds them with the same copy
        // actions on top. See the comment there for why nothing is lost.
        // A bottom row has to be reserved BEFORE the plot claims the remaining height, or the plot fills
        // everything and pushes the buttons out of the window. SmallButton uses zero vertical frame
        // padding, so its height is one text line.
        const float BottomRowHeight = (bShowButtons && ButtonLocation == EMobiusCopyButtonLocation::Bottom)
                ? ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0f
                : 0.0f;

        // Negative width/height means "fill, minus this much", so the plot reflows around whatever the
        // buttons take: all the remaining space, less any side column or bottom row reserved for them.
        if (ImPlot::BeginPlot("##MobiusPlot", ImVec2(-1.0f - SideColumnWidth, -1.0f - BottomRowHeight), ImPlotFlags_NoMenus))
        {
                if (HasAxisSettingsForChart(ChartId))
                {
                        const FString XTitleString = GetXAxisTitleForChart(ChartId).ToString();
                        const FString YTitleString = GetYAxisTitleForChart(ChartId).ToString();
                        ImPlot::SetupAxes(
                                XTitleString.IsEmpty() ? nullptr : TCHAR_TO_UTF8(*XTitleString),
                                YTitleString.IsEmpty() ? nullptr : TCHAR_TO_UTF8(*YTitleString),
                                ImPlotAxisFlags_AutoFit,
                                ImPlotAxisFlags_AutoFit);

                        double OutXMin = 0.0;
                        double OutXMax = 0.0;
                        double OutYMin = 0.0;
                        double OutYMax = 0.0;
                        GetAxisLimitsForChart(ChartId, OutXMin, OutXMax, OutYMin, OutYMax);
                        ImPlot::SetupAxesLimits(OutXMin, OutXMax, OutYMin, OutYMax, ImPlotCond_Always);
                }

                const TArray<FVector2D>& Points = GetPlotPointsForChart(ChartId);
                if (Points.Num() > 0)
                {
                        TArray<double> XValues;
                        TArray<double> YValues;
                        XValues.SetNum(Points.Num());
                        YValues.SetNum(Points.Num());
                        for (int32 Index = 0; Index < Points.Num(); ++Index)
                        {
                                XValues[Index] = Points[Index].X;
                                YValues[Index] = Points[Index].Y;
                        }

                        ImPlot::PlotLine("Evacuated", XValues.GetData(), YValues.GetData(), XValues.Num());
                }

                double LiveX = 0.0;
                double LiveY = 0.0;
                const bool bHasLiveSample = HasLiveSampleForChart(ChartId);
                // Left out of a copied image on purpose. The playhead marks where playback happens to be
                // sitting at the moment of the copy, which means nothing once the picture is in a document
                // — and on a chart with no data yet it was the ONLY thing in the image, so an empty copy
                // came out as a legend entry reading "Live" and nothing else.
                if (bHasLiveSample && !bCapturingForImageCopy)
                {
                        GetLiveSampleForChart(ChartId, LiveX, LiveY);
                        ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), 2.0f);
                        ImPlot::PlotInfLines("Live##CurrentTime", &LiveX, 1);
                }

                if (ImPlot::IsPlotHovered())
                {
                        const ImPlotPoint MousePos = ImPlot::GetPlotMousePos();
                        FVector2D NearestPoint = FVector2D::ZeroVector;
                        const bool bHasNearest = TryGetNearestPointForChart(ChartId, MousePos.x, NearestPoint);
                        double TotalAgents = 0.0;
                        if (HasAxisSettingsForChart(ChartId))
                        {
                                double XMin = 0.0;
                                double XMax = 0.0;
                                double YMin = 0.0;
                                double YMax = 0.0;
                                GetAxisLimitsForChart(ChartId, XMin, XMax, YMin, YMax);
                                TotalAgents = YMax;
                        }
                        const double EvacuatedValue = bHasNearest
                                ? NearestPoint.Y
                                : (bHasLiveSample && TotalAgents > 0.0 ? TotalAgents - LiveY : 0.0);
                        const double RemainingValue = TotalAgents > 0.0
                                ? FMath::Max(0.0, TotalAgents - EvacuatedValue)
                                : 0.0;
                        ImGui::BeginTooltip();
                        ImGui::Text("Time: %.2f", MousePos.x);
                        if (bHasNearest || bHasLiveSample)
                        {
                                ImGui::Text("Evacuated: %.0f", EvacuatedValue);
                                if (TotalAgents > 0.0)
                                {
                                        ImGui::Text("Remaining: %.0f", RemainingValue);
                                }
                        }
                        ImGui::EndTooltip();
                }

                // S6: the plot's right-click menu, rebuilt.
                //
                // ImPlotFlags_NoMenus on BeginPlot above suppressed ImPlot's own popups because 0.17 has no
                // way to append to them. Everything ImPlot would have drawn is reproduced here from its
                // exported internals — ShowPlotContextMenu and ShowAxisContextMenu are both IMPLOT_API, so
                // this needs NO patch to the vendored library and no copied menu code that could drift from
                // it. The axis popups are rebuilt too: NoMenus kills those as well, and dropping
                // right-click-on-an-axis would be a silent regression on a chart people zoom.
                //
                // The open condition mirrors implot.cpp's own `can_ctx` exactly, including the
                // legend-hovered exclusion. GImPlot->OpenContextThisFrame is set by UpdateInput() during
                // BeginPlot and cleared while selecting or panning, which is what stops a right-drag
                // box-zoom from also opening the menu — reimplementing that from IsMouseReleased would get
                // it wrong.
                if (ImPlotPlot* CurrentPlot = ImPlot::GetCurrentPlot())
                {
                        const bool bCanOpenContext = GImPlot != nullptr
                                && GImPlot->OpenContextThisFrame
                                && !CurrentPlot->Items.Legend.Hovered;
                        const bool bAxisEqual = ImHasFlag(CurrentPlot->Flags, ImPlotFlags_Equal);

                        // PushOverrideID(plot.ID) is what ImPlot does, so the popup IDs match the plot
                        // rather than wherever this happens to sit on the ID stack.
                        ImGui::PushOverrideID(CurrentPlot->ID);

                        // "##Mobius…" NOT ImPlot's own "##PlotContext"/"##XContext"/"##YContext".
                        // ImPlotFlags_NoMenus gates only the OpenPopup calls in EndPlot — the matching
                        // `if (ImGui::BeginPopup("##PlotContext"))` runs UNCONDITIONALLY. Under the same
                        // PushOverrideID, reusing the name meant EndPlot re-entered the popup this block
                        // had just opened and drew ShowPlotContextMenu a SECOND time, which Dear ImGui
                        // reports as "2 visible items with conflicting ID". Distinct names leave ImPlot's
                        // BeginPopup returning false, as NoMenus intends.
                        if (bCanOpenContext && CurrentPlot->Hovered)
                        {
                                ImGui::OpenPopup("##MobiusPlotContext");
                        }
                        if (ImGui::BeginPopup("##MobiusPlotContext"))
                        {
                                // A SUBMENU of settings, not the copy actions themselves. Everything else in
                                // this menu — Legend, Settings, the per-axis entries — adjusts the chart, so
                                // two verbs sitting among them read as a different kind of thing. The actions
                                // are the buttons; this is where their behaviour is configured. One entry
                                // today, and it is the right shape for the next one.
                                if (ImGui::BeginMenu("Copy Settings"))
                                {
                                        ImGui::MenuItem("Copy with timeline", nullptr, &State->bCopyWithTimeline);
                                        ImGui::SetItemTooltip("Include the time column when copying values");

                                        ImGui::Separator();
                                        ImGui::TextUnformatted("Buttons");

                                        // Same idiom as ImPlot's own legend location picker
                                        // (ShowLegendContextMenu): a grid of small buttons laid out where the
                                        // thing will end up, so the control looks like what it does. Four
                                        // edges rather than nine positions — these buttons sit outside the
                                        // plot, so a corner has no meaning.
                                        const float ButtonSize = ImGui::GetFrameHeight();
                                        const ImVec2 CellSize(1.5f * ButtonSize, ButtonSize);
                                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));

                                        ImGui::InvisibleButton("##NW", CellSize); ImGui::SameLine();
                                        if (ImGui::Button("T", CellSize)) { State->CopyButtonLocation = EMobiusCopyButtonLocation::Top; }
                                        ImGui::SameLine();
                                        ImGui::InvisibleButton("##NE", CellSize);

                                        if (ImGui::Button("L", CellSize)) { State->CopyButtonLocation = EMobiusCopyButtonLocation::Left; }
                                        ImGui::SameLine();
                                        ImGui::InvisibleButton("##C", CellSize); ImGui::SameLine();
                                        if (ImGui::Button("R", CellSize)) { State->CopyButtonLocation = EMobiusCopyButtonLocation::Right; }

                                        ImGui::InvisibleButton("##SW", CellSize); ImGui::SameLine();
                                        if (ImGui::Button("B", CellSize)) { State->CopyButtonLocation = EMobiusCopyButtonLocation::Bottom; }
                                        ImGui::SameLine();
                                        ImGui::InvisibleButton("##SE", CellSize);

                                        ImGui::PopStyleVar();
                                        ImGui::EndMenu();
                                }
                                ImGui::Separator();
                                ImPlot::ShowPlotContextMenu(*CurrentPlot);
                                ImGui::EndPopup();
                        }

                        for (int32 AxisIndex = 0; AxisIndex < IMPLOT_NUM_X_AXES; ++AxisIndex)
                        {
                                ImGui::PushID(AxisIndex);
                                ImPlotAxis& XAxis = CurrentPlot->XAxis(AxisIndex);
                                if (bCanOpenContext && XAxis.Hovered && XAxis.HasMenus())
                                {
                                        ImGui::OpenPopup("##MobiusXContext");
                                }
                                if (ImGui::BeginPopup("##MobiusXContext"))
                                {
                                        ImGui::TextUnformatted(XAxis.HasLabel()
                                                ? CurrentPlot->GetAxisLabel(XAxis) : "X-Axis");
                                        ImGui::Separator();
                                        ImPlot::ShowAxisContextMenu(XAxis, bAxisEqual ? XAxis.OrthoAxis : nullptr, true);
                                        ImGui::EndPopup();
                                }
                                ImGui::PopID();
                        }
                        for (int32 AxisIndex = 0; AxisIndex < IMPLOT_NUM_Y_AXES; ++AxisIndex)
                        {
                                ImGui::PushID(AxisIndex);
                                ImPlotAxis& YAxis = CurrentPlot->YAxis(AxisIndex);
                                if (bCanOpenContext && YAxis.Hovered && YAxis.HasMenus())
                                {
                                        ImGui::OpenPopup("##MobiusYContext");
                                }
                                if (ImGui::BeginPopup("##MobiusYContext"))
                                {
                                        ImGui::TextUnformatted(YAxis.HasLabel()
                                                ? CurrentPlot->GetAxisLabel(YAxis) : "Y-Axis");
                                        ImGui::Separator();
                                        ImPlot::ShowAxisContextMenu(YAxis, bAxisEqual ? YAxis.OrthoAxis : nullptr, false);
                                        ImGui::EndPopup();
                                }
                                ImGui::PopID();
                        }

                        ImGui::PopID();
                }

                ImPlot::EndPlot();
        }

        if (bShowButtons && ButtonLocation == EMobiusCopyButtonLocation::Right)
        {
                // SameLine pairs with the width the plot gave up via SideColumnWidth above.
                ImGui::SameLine();
                ImGui::BeginGroup();
                DrawCopyButtons(/*bStacked*/true);
                ImGui::EndGroup();
        }
        else if (bShowButtons && ButtonLocation == EMobiusCopyButtonLocation::Bottom)
        {
                DrawCopyButtons(/*bStacked*/false);
        }

        ImGui::End();
        if (State->bWindowOpen && !bOpen)
        {
                State->bWindowOpen = false;
                if (OnRequestClose.IsBound())
                {
                        OnRequestClose.Execute();
                }
        }
        ImGui::Render();

        const ImDrawData* DrawData = ImGui::GetDrawData();
        const FVector2f WindowOffset = FVector2f(AllottedGeometry.GetAccumulatedLayoutTransform().GetTranslation());

        // Get DPI scale for rendering - needed to scale ImGui output to match Slate's render surface
        float RenderDpiScale = 1.0f;
        if (FSlateApplication::IsInitialized())
        {
                const TSharedPtr<SWindow> RenderWindow = FSlateApplication::Get().FindWidgetWindow(Widget);
                if (RenderWindow.IsValid())
                {
                        RenderDpiScale = RenderWindow->GetDPIScaleFactor();
                }
        }

        RenderDrawData(DrawData, WindowOffset, RenderDpiScale, OutDrawElements, LayerId + 1);

        return LayerId + 1;
}

void UImPlotVisualizationSubsystem::EnsureOverlayContext(FImPlotOverlayState& State)
{
        if (State.ImGuiContext && State.ImPlotContext)
        {
                return;
        }
        if (!SharedFontAtlas)
        {
                SharedFontAtlas = IM_NEW(ImFontAtlas)();
        }

        State.ImGuiContext = ImGui::CreateContext(SharedFontAtlas);
        State.ImPlotContext = ImPlot::CreateContext();
        ImGui::SetCurrentContext(State.ImGuiContext);
        ImPlot::SetCurrentContext(State.ImPlotContext);

        ImGuiIO& IO = ImGui::GetIO();
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        IO.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        EnsureSharedFontAtlas();
}

void UImPlotVisualizationSubsystem::DestroyOverlayContext(FImPlotOverlayState& State)
{
        if (State.ImGuiContext)
        {
                const bool bReleaseSharedAtlas = SharedFontAtlas && SharedFontAtlas->RefCount == 1;
                ImGui::SetCurrentContext(State.ImGuiContext);
                if (State.ImPlotContext)
                {
                        ImPlot::DestroyContext(State.ImPlotContext);
                        State.ImPlotContext = nullptr;
                }
                ImGui::DestroyContext(State.ImGuiContext);
                State.ImGuiContext = nullptr;
                if (bReleaseSharedAtlas)
                {
                        SharedFontAtlas = nullptr;
                        SharedFontBrush.Reset();
                        SharedFontTextureId = 0;
                        SharedFontTextureName = NAME_None;
                }
        }
        State.bWindowOpen = true;
}

void UImPlotVisualizationSubsystem::EnsureSharedFontAtlas(float InDpiScale)
{
        if (SharedFontBrush.IsValid() || !FSlateApplication::IsInitialized())
        {
                return;
        }
        if (!SharedFontAtlas)
        {
                SharedFontAtlas = IM_NEW(ImFontAtlas)();
        }

        // Bake glyphs at the window's DPI scale so they are rasterized at their final on-screen pixel
        // size. They draw at 13 logical units (FontScaleMain = 1/scale in PaintOverlayForChart) and the
        // vertex upscale in RenderDrawData maps them 1:1 to physical pixels. Without this the default
        // 13px bake gets bitmap-stretched 4x at 400% OS scaling.
        SharedFontAtlasDpiScale = FMath::Max(InDpiScale, 1.0f);
        if (SharedFontAtlas->Fonts.Size == 0)
        {
                const float FontPixelSize = FMath::RoundToFloat(13.0f * SharedFontAtlasDpiScale);
                // Prefer the engine-shipped Roboto over ImGui's embedded ProggyClean: Proggy is a
                // pixel font designed for exactly 13px and rasterizes poorly at other sizes.
                const FString RobotoPath = FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf");
                if (FPaths::FileExists(RobotoPath))
                {
                        SharedFontAtlas->AddFontFromFileTTF(TCHAR_TO_UTF8(*RobotoPath), FontPixelSize);
                }
                else
                {
                        ImFontConfig FontConfig;
                        FontConfig.SizePixels = FontPixelSize;
                        SharedFontAtlas->AddFontDefault(&FontConfig);
                }
        }

        unsigned char* Pixels = nullptr;
        int32 Width = 0;
        int32 Height = 0;
        SharedFontAtlas->GetTexDataAsRGBA32(&Pixels, &Width, &Height);

        if (!Pixels || Width <= 0 || Height <= 0)
        {
                return;
        }

        TArray<uint8> ImageData;
        ImageData.Append(Pixels, Width * Height * 4);

        if (SharedFontTextureName.IsNone())
        {
                // Unique per bake: dynamic image brushes register their texture under this name, so
                // reusing it after a DPI rebake could resolve to the stale texture.
                static uint32 AtlasBakeCounter = 0;
                SharedFontTextureName = FName(*FString::Printf(TEXT("ImGuiFontAtlas_Shared_%p_%u"), this, ++AtlasBakeCounter));
        }

        SharedFontBrush = FSlateDynamicImageBrush::CreateWithImageData(SharedFontTextureName, FVector2D(Width, Height), ImageData);
        if (SharedFontBrush.IsValid())
        {
                SharedFontTextureId = static_cast<uint64>(reinterpret_cast<uintptr_t>(SharedFontBrush.Get()));
                SharedFontAtlas->SetTexID(static_cast<ImTextureID>(SharedFontTextureId));
        }
}

bool UImPlotVisualizationSubsystem::TryGetNearestPointForChart(const FName& ChartId, double TimeSeconds, FVector2D& OutPoint) const
{
        const TArray<FVector2D>& Points = GetPlotPointsForChart(ChartId);
        if (Points.Num() == 0)
        {
                return false;
        }

        double BestDistance = TNumericLimits<double>::Max();
        for (const FVector2D& Point : Points)
        {
                const double Distance = FMath::Abs(Point.X - TimeSeconds);
                if (Distance < BestDistance)
                {
                        BestDistance = Distance;
                        OutPoint = Point;
                }
        }

        return true;
}

void UImPlotVisualizationSubsystem::RenderDrawData(const ImDrawData* DrawData, const FVector2f& WindowOffset,
        float DpiScale, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
        if (!DrawData || !SharedFontBrush.IsValid())
        {
                return;
        }

        if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
        {
                return;
        }

        const FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*SharedFontBrush);
        const FSlateRenderTransform RenderTransform;

        for (int32 ListIndex = 0; ListIndex < DrawData->CmdListsCount; ++ListIndex)
        {
                const ImDrawList* DrawList = DrawData->CmdLists[ListIndex];
                const ImDrawVert* Vertices = DrawList->VtxBuffer.Data;
                const ImDrawIdx* Indices = DrawList->IdxBuffer.Data;

                for (int32 CmdIndex = 0; CmdIndex < DrawList->CmdBuffer.Size; ++CmdIndex)
                {
                        const ImDrawCmd& DrawCmd = DrawList->CmdBuffer[CmdIndex];
                        if (DrawCmd.UserCallback != nullptr || DrawCmd.ElemCount == 0)
                        {
                                continue;
                        }

                        const ImTextureID CmdTexId = DrawCmd.GetTexID();
                        if (CmdTexId != static_cast<ImTextureID>(SharedFontTextureId) && CmdTexId != ImTextureID_Invalid)
                        {
                                continue;
                        }

                        TArray<FSlateVertex> CmdVertices;
                        TArray<SlateIndex> CmdIndices;
                        CmdVertices.Reserve(DrawCmd.ElemCount);
                        CmdIndices.Reserve(DrawCmd.ElemCount);

                        for (uint32 ElemIndex = 0; ElemIndex < DrawCmd.ElemCount; ++ElemIndex)
                        {
                                const ImDrawIdx DrawIndex = Indices[DrawCmd.IdxOffset + ElemIndex] + static_cast<ImDrawIdx>(DrawCmd.VtxOffset);
                                const ImDrawVert& Vertex = Vertices[DrawIndex];

                                const uint8 R = (Vertex.col >> IM_COL32_R_SHIFT) & 0xFF;
                                const uint8 G = (Vertex.col >> IM_COL32_G_SHIFT) & 0xFF;
                                const uint8 B = (Vertex.col >> IM_COL32_B_SHIFT) & 0xFF;
                                const uint8 A = (Vertex.col >> IM_COL32_A_SHIFT) & 0xFF;
                                const FColor Color(R, G, B, A);

                                // Scale ImGui vertices by DPI to match Slate's physical render surface
                                const FVector2f Position = FVector2f(Vertex.pos.x * DpiScale, Vertex.pos.y * DpiScale) + WindowOffset;
                                const FVector2f UV(Vertex.uv.x, Vertex.uv.y);

                                CmdVertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, Position, UV, Color));
                                CmdIndices.Add(static_cast<SlateIndex>(ElemIndex));
                        }

                        // Scale clip rect by DPI to match scaled vertices
                        const FSlateRect ClipRect(
                                DrawCmd.ClipRect.x * DpiScale + WindowOffset.X,
                                DrawCmd.ClipRect.y * DpiScale + WindowOffset.Y,
                                DrawCmd.ClipRect.z * DpiScale + WindowOffset.X,
                                DrawCmd.ClipRect.w * DpiScale + WindowOffset.Y);

                        OutDrawElements.PushClip(FSlateClippingZone(ClipRect));
                        FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, ResourceHandle, CmdVertices, CmdIndices, nullptr, 0, 0);
                        OutDrawElements.PopClip();
                }
        }
}




