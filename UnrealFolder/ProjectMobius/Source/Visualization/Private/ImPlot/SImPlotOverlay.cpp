// Fill out your copyright notice in the Description page of Project Settings.

#include "ImPlot/SImPlotOverlay.h"
#include "ImPlot/ImPlotVisualizationSubsystem.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Layout/Clipping.h"
#include "Misc/App.h"
#include "Rendering/DrawElementTypes.h"
#include "Rendering/RenderingCommon.h"
#include "Widgets/SWindow.h"
#include "Widgets/SNullWidget.h"

#include "imgui.h"
#include "implot.h"

SImPlotOverlay::SImPlotOverlay()
{
}

SImPlotOverlay::~SImPlotOverlay()
{
	if (ImGuiContext)
	{
		ImGui::SetCurrentContext(ImGuiContext);
		if (ImPlotContext)
		{
			ImPlot::DestroyContext(ImPlotContext);
			ImPlotContext = nullptr;
		}
		ImGui::DestroyContext(ImGuiContext);
		ImGuiContext = nullptr;
	}
}

void SImPlotOverlay::Construct(const FArguments& InArgs)
{
        Subsystem = InArgs._Subsystem;
        OnRequestClose = InArgs._OnRequestClose;

        ChildSlot
        [
                SNullWidget::NullWidget
        ];

        ImGuiContext = ImGui::CreateContext();
        ImPlotContext = ImPlot::CreateContext();
        ImGui::SetCurrentContext(ImGuiContext);
	ImPlot::SetCurrentContext(ImPlotContext);

        ImGuiIO& IO = ImGui::GetIO();
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        IO.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        BuildFontAtlas();
}

void SImPlotOverlay::ResetWindowState()
{
        bWindowOpen = true;
}

int32 SImPlotOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                              FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                              bool bParentEnabled) const
{
	if (!ImGuiContext || !ImPlotContext)
	{
		return LayerId;
	}

	if (!Subsystem.IsValid() || !Subsystem->IsOverlayVisible())
	{
		return LayerId;
	}

        ImGui::SetCurrentContext(ImGuiContext);
        ImPlot::SetCurrentContext(ImPlotContext);

        ImGuiIO& IO = ImGui::GetIO();
        if (FSlateApplication::IsInitialized())
        {
                const FVector2f CursorPos = FVector2f(FSlateApplication::Get().GetCursorPos());
                const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
                if (Window.IsValid())
                {
                        const FSlateRect ClientRect = Window->GetClientRectInScreen();
                        const float DpiScale = Window->GetDPIScaleFactor();
                        const FVector2f ClientOrigin = FVector2f(ClientRect.Left, ClientRect.Top);
                        const FVector2f LocalCursorPos = (CursorPos - ClientOrigin) / DpiScale;
                        IO.MousePos = ImVec2(LocalCursorPos.X, LocalCursorPos.Y);
                        const FVector2f ClientSize = FVector2f(ClientRect.GetSize()) / DpiScale;
                        IO.DisplaySize = ImVec2(ClientSize.X, ClientSize.Y);
                }
                else
                {
                        const FVector2f LocalCursorPos = FVector2f(AllottedGeometry.AbsoluteToLocal(CursorPos));
                        IO.MousePos = ImVec2(LocalCursorPos.X, LocalCursorPos.Y);
                }
                IO.MouseDown[0] = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
                IO.MouseDown[1] = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::RightMouseButton);
                IO.MouseDown[2] = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::MiddleMouseButton);
        }
        const FVector2f LocalSize = FVector2f(AllottedGeometry.GetLocalSize());
        if (IO.DisplaySize.x == 0.0f && IO.DisplaySize.y == 0.0f)
        {
                IO.DisplaySize = ImVec2(LocalSize.X, LocalSize.Y);
        }
        IO.DeltaTime = FMath::Max(1.0e-6f, static_cast<float>(FApp::GetDeltaTime()));

        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(LocalSize.X, LocalSize.Y), ImGuiCond_Always);
        bool bOpen = bWindowOpen;
        ImGui::Begin("UE Plot Overlay", &bOpen,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);

        const FString StatusString = Subsystem->GetStatusMessage().ToString();
        const FString TitleString = Subsystem->GetChartTitle().ToString();
        if (!StatusString.IsEmpty())
        {
                ImGui::TextUnformatted(TCHAR_TO_UTF8(*StatusString));
        }
        else
        {
                ImGui::TextUnformatted("Status: (none)");
        }

        if (!TitleString.IsEmpty())
        {
                ImGui::Spacing();
                ImGui::TextUnformatted(TCHAR_TO_UTF8(*TitleString));
        }

	if (ImPlot::BeginPlot("##MobiusPlot", ImVec2(-1.0f, -1.0f)))
	{
		if (Subsystem->HasAxisSettings())
		{
			const FString XTitleString = Subsystem->GetXAxisTitle().ToString();
			const FString YTitleString = Subsystem->GetYAxisTitle().ToString();
			ImPlot::SetupAxes(
				XTitleString.IsEmpty() ? nullptr : TCHAR_TO_UTF8(*XTitleString),
				YTitleString.IsEmpty() ? nullptr : TCHAR_TO_UTF8(*YTitleString),
				ImPlotAxisFlags_AutoFit,
				ImPlotAxisFlags_AutoFit);

			double OutXMin = 0.0;
			double OutXMax = 0.0;
			double OutYMin = 0.0;
			double OutYMax = 0.0;
			Subsystem->GetAxisLimits(OutXMin, OutXMax, OutYMin, OutYMax);
			ImPlot::SetupAxesLimits(OutXMin, OutXMax, OutYMin, OutYMax, ImPlotCond_Always);
		}

                const TArray<FVector2D>& Points = Subsystem->GetPlotPoints();
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
                const bool bHasLiveSample = Subsystem->HasLiveSample();
                if (bHasLiveSample)
                {
                        Subsystem->GetLiveSample(LiveX, LiveY);
                        const ImPlotRect PlotLimits = ImPlot::GetPlotLimits();
                        double LiveThickness = Subsystem->HasLiveSampleThickness()
                                ? Subsystem->GetLiveSampleThickness()
                                : FMath::Max(KINDA_SMALL_NUMBER, (PlotLimits.X.Max - PlotLimits.X.Min) * 0.01);
                        const double HalfWidth = LiveThickness * 0.5;
                        const double BandX[2] = { LiveX - HalfWidth, LiveX + HalfWidth };
                        const double BandYMin[2] = { PlotLimits.Y.Min, PlotLimits.Y.Min };
                        const double BandYMax[2] = { PlotLimits.Y.Max, PlotLimits.Y.Max };
                        ImPlot::SetNextLineStyle(ImVec4(0.85f, 0.1f, 0.1f, 0.9f), 1.5f);
                        ImPlot::PlotInfLines("Live", &LiveX, 1);
                }

                if (ImPlot::IsPlotHovered())
                {
                        const ImPlotPoint MousePos = ImPlot::GetPlotMousePos();
                        FVector2D NearestPoint;
                        const bool bHasNearest = TryGetNearestPoint(MousePos.x, NearestPoint);
                        double TotalAgents = 0.0;
                        if (Subsystem->HasAxisSettings())
                        {
                                double XMin = 0.0;
                                double XMax = 0.0;
                                double YMin = 0.0;
                                double YMax = 0.0;
                                Subsystem->GetAxisLimits(XMin, XMax, YMin, YMax);
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

                ImPlot::EndPlot();
        }

        ImGui::End();
        if (bWindowOpen && !bOpen)
        {
                bWindowOpen = false;
                if (OnRequestClose.IsBound())
                {
                        OnRequestClose.Execute();
                }
        }
        ImGui::Render();

	const ImDrawData* DrawData = ImGui::GetDrawData();
	const FVector2f WindowOffset = FVector2f(AllottedGeometry.GetAccumulatedLayoutTransform().GetTranslation());
        RenderDrawData(DrawData, WindowOffset, OutDrawElements, LayerId + 1);

        return LayerId + 1;
}

bool SImPlotOverlay::TryGetNearestPoint(double TimeSeconds, FVector2D& OutPoint) const
{
        if (!Subsystem.IsValid())
        {
                return false;
        }

        const TArray<FVector2D>& Points = Subsystem->GetPlotPoints();
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

void SImPlotOverlay::BuildFontAtlas()
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	ImGui::SetCurrentContext(ImGuiContext);
	ImGuiIO& IO = ImGui::GetIO();

	unsigned char* Pixels = nullptr;
	int32 Width = 0;
	int32 Height = 0;
	IO.Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);

	if (!Pixels || Width <= 0 || Height <= 0)
	{
		return;
	}

	TArray<uint8> ImageData;
	ImageData.Append(Pixels, Width * Height * 4);

	FontTextureName = FName(*FString::Printf(TEXT("ImGuiFontAtlas_%p"), this));
	FontBrush = FSlateDynamicImageBrush::CreateWithImageData(FontTextureName, FVector2D(Width, Height), ImageData);
	if (FontBrush.IsValid())
	{
		FontResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FontBrush);
		FontTextureId = static_cast<uint64>(reinterpret_cast<uintptr_t>(FontBrush.Get()));
		IO.Fonts->SetTexID(static_cast<ImTextureID>(FontTextureId));
	}
}

void SImPlotOverlay::RenderDrawData(const ImDrawData* DrawData, const FVector2f& WindowOffset,
                                    FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!DrawData || !FontBrush.IsValid())
	{
		return;
	}

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
			if (CmdTexId != static_cast<ImTextureID>(FontTextureId) && CmdTexId != ImTextureID_Invalid)
			{
				continue;
			}

			const FSlateResourceHandle ResourceHandle = FontResourceHandle;

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

				const FVector2f Position = FVector2f(Vertex.pos.x, Vertex.pos.y) + WindowOffset;
				const FVector2f UV(Vertex.uv.x, Vertex.uv.y);

				CmdVertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, Position, UV, Color));
				CmdIndices.Add(static_cast<SlateIndex>(ElemIndex));
			}

			const FSlateRect ClipRect(
				DrawCmd.ClipRect.x + WindowOffset.X,
				DrawCmd.ClipRect.y + WindowOffset.Y,
				DrawCmd.ClipRect.z + WindowOffset.X,
				DrawCmd.ClipRect.w + WindowOffset.Y);

			OutDrawElements.PushClip(FSlateClippingZone(ClipRect));
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, ResourceHandle, CmdVertices, CmdIndices, nullptr, 0, 0);
			OutDrawElements.PopClip();
		}
	}
}
