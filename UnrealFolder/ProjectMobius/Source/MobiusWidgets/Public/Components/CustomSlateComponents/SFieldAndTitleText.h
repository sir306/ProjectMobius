// copyright
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class MOBIUSWIDGETS_API SFieldAndTitleText final : public SCompoundWidget
{
public:
	/**  */
	SLATE_BEGIN_ARGS(SFieldAndTitleText)
		: _FieldText()
		, _TitleText()
		, _TextStyle( &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>( "NormalText" ) )
		, _VerticalStacking(false)
	{
		
	}
		/** The text to display in the field */
		SLATE_ATTRIBUTE(FText, FieldText)

		/** The title text to display above the field */
		SLATE_ATTRIBUTE(FText, TitleText)
		
		/** Pointer to a style of the text block, which dictates the font, color, and shadow options. */
		SLATE_STYLE_ARGUMENT( FTextBlockStyle, TextStyle )
		
		/** Whether the title is above the field or to the left */
		SLATE_ATTRIBUTE(bool, VerticalStacking)
		
	SLATE_END_ARGS()

/** Default constructor for SFieldAndTitleText */
SFieldAndTitleText();

/** Destructor */
~SFieldAndTitleText();

/** Constructs and initializes the widget
 * @param InArgs The declaration data for this widget
 */
void Construct(const FArguments& InArgs);
	void SetTitleText(FText InTitleText);
	void SetFieldText(FText InFieldText);

	/** Paints this widget in the game viewport
 * @param Args The paint arguments
 * @param AllottedGeometry The space allotted for this widget
 * @param MyCullingRect The culling rect for this widget
 * @param OutDrawElements A list of elements to draw
 * @param LayerId The layer to draw on
 * @param InWidgetStyle The style for the widget
 * @param bParentEnabled True if the parent is enabled
 * @return The layer ID that was drawn on
 */
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;


private:
	TSharedPtr<STextBlock> TitleTextBlock;
	TSharedPtr<STextBlock> FieldTextBlock;
};
