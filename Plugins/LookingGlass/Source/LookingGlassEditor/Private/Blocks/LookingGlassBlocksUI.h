#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FLookingGlassBlocksInterface;
struct FLookingGlassBlocksUploadResult;

class SBlocksUploadWindow : public SCompoundWidget
{
public:
	// Entry point for UI
	static void ShowWindow(FLookingGlassBlocksInterface* BlocksInterface);

	SLATE_BEGIN_ARGS(SBlocksUploadWindow)
		: _WidgetWindow()
		, _BlocksInterface(nullptr)
		{}

		SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
		SLATE_ARGUMENT(FLookingGlassBlocksInterface*, BlocksInterface)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SWindow>& InParentWindow);

	SBlocksUploadWindow()
		: UploadProgress(-1.0f)
	{}

protected:

	// Window that owns us
	TSharedPtr<SWindow> WidgetWindow;

	// FLookingGlassBlocksInterface which we're interacting with
	FLookingGlassBlocksInterface* BlocksInterface;

	// UI callbacks

	void OnWindowClosed(const TSharedRef<SWindow>& Window);

	FReply OnLoginClicked();

	FReply OnTakeScreenshotClicked();

	FReply OnUploadClicked();

	void OnUploadCompleted(const FLookingGlassBlocksUploadResult& Result);

	FText GetLoginButtonText() const;

	FText GetLoginPromptText() const;

	bool IsLoggedIn() const;

	bool CanUpload() const;

	bool CanTakeScreenshot() const;

	FString GetImageFilePath() const;

	void SetImageFilePath(const FString& PickedPath);

	FText GetDescriptionText() const;

	FText GetDisplayUrl() const;

	void OnDescriptionTextCommitted(const FText& Text, ETextCommit::Type CommitInfo);

	FText GetTitleText() const;

	void OnTitleTextCommitted(const FText& Text, ETextCommit::Type CommitInfo);

	// Information stored in UI

	FString ImageFilePath;

	FString ImageTitle;

	FString ImageDescription;

	float UploadProgress;

	FString UploadedImageUrl;

	FText UploadErrorMessage;

	// Statics for singleton-like access

	static TSharedPtr<SWindow> BlocksWindowPtr;

	static TSharedPtr<SBlocksUploadWindow> BlocksWindowContent;
};
