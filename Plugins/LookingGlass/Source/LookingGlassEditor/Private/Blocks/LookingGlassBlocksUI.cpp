#include "LookingGlassBlocksUI.h"
#include "LookingGlassBlocksInterface.h"

// Interface for taking screenshots
#include "ILookingGlassRuntime.h"
#include "Render/SLookingGlassViewport.h"
#include "Render/LookingGlassViewportClient.h"

#include "Modules/ModuleManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Framework/Application/SlateApplication.h"

#include "Engine/Engine.h"	// just for GameScreenshotSaveDirectory access

#include "Styling/SlateTypes.h"
#include "SlateOptMacros.h"
#include "EditorStyleSet.h"

// Widgets used in this UI
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SFilePathPicker.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Input/SHyperlink.h"

#define LOCTEXT_NAMESPACE "SBlocksUploadWindow"

TSharedPtr<SWindow> SBlocksUploadWindow::BlocksWindowPtr;
TSharedPtr<SBlocksUploadWindow> SBlocksUploadWindow::BlocksWindowContent;

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SBlocksUploadWindow::Construct(const FArguments& InArgs, const TSharedRef<SWindow>& InParentWindow)
{
	WidgetWindow = InArgs._WidgetWindow;
	BlocksInterface = InArgs._BlocksInterface;

	InParentWindow->SetOnWindowClosed(FOnWindowClosed::CreateSP(this, &SBlocksUploadWindow::OnWindowClosed));

	this->ChildSlot
	[
		SNew(SVerticalBox)
		// Login box
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.Visibility(EVisibility::Visible)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
//					.Font(FAppStyle::GetFontStyle("CurveEd.LabelFont"))
					.Text(this, &SBlocksUploadWindow::GetLoginPromptText)
				]
				+SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.AutoWidth()
				[
					SNew(SButton)
					.OnClicked(this, &SBlocksUploadWindow::OnLoginClicked)
					.Content()
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.MinDesiredWidth(100)
						.Text(this, &SBlocksUploadWindow::GetLoginButtonText)
					]
				]
			]
		]
		// Take screenshot button
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			[
				SNew(SButton)
				.IsEnabled(this, &SBlocksUploadWindow::CanTakeScreenshot)
				.OnClicked(this, &SBlocksUploadWindow::OnTakeScreenshotClicked)
				.HAlign(HAlign_Center)
				.Content()
				[
					SNew(STextBlock)
					.Justification(ETextJustify::Center)
					.MinDesiredWidth(150)
					.Text(LOCTEXT("Blocks_TakeScreenshot", "Capture Screenshot"))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SGridPanel)
			.IsEnabled(this, &SBlocksUploadWindow::IsLoggedIn)
			.FillColumn(0, 0.5f)
			.FillColumn(1, 1.0f)
			// File picker
			+SGridPanel::Slot(0, 0)
			.Padding(5)
			[
				SNew(STextBlock)
				.Justification(ETextJustify::Right)
				.Text(LOCTEXT("Blocks_PickFile", "File to Upload"))
			]
			+SGridPanel::Slot(1, 0)
			.Padding(5)
			[
				SNew(SFilePathPicker)
				// It's required to explicitly set style, otherwise SFilePathPicker will crash
				.BrowseButtonImage(FAppStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
				.BrowseButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				// Pick files only from screenshot directory, see FLookingGlassScreenshotRequest::CreateViewportScreenShotFilename
				.BrowseDirectory(GetDefault<UEngine>()->GameScreenshotSaveDirectory.Path)
				// Filter files by having quilt settings encoded in file name
				.FileTypeFilter(TEXT("Quilt Screenshots|*_qs*x*.*"))
				// Accessors for the ImageFilePath property
				.FilePath(this, &SBlocksUploadWindow::GetImageFilePath)
				.OnPathPicked(this, &SBlocksUploadWindow::SetImageFilePath)
			]
			// Block title editor
			+SGridPanel::Slot(0, 1)
			.Padding(5)
			[
				SNew(STextBlock)
				.Justification(ETextJustify::Right)
				.Text(LOCTEXT("Blocks_Title", "Block Title"))
			]
			+SGridPanel::Slot(1, 1)
			.Padding(5)
			[
				SNew(SEditableTextBox)
				.Text(this, &SBlocksUploadWindow::GetTitleText)
				.OnTextCommitted(this, &SBlocksUploadWindow::OnTitleTextCommitted)
			]
			// Block description editor
			+SGridPanel::Slot(0, 2)
			.Padding(5)
			[
				SNew(STextBlock)
				.Justification(ETextJustify::Right)
				.Text(LOCTEXT("Blocks_Description", "Block Description"))
			]
			+SGridPanel::Slot(1, 2)
			.Padding(5)
			[
				SNew(SBox)
				.HeightOverride(60.0f)
				[
					SNew(SMultiLineEditableTextBox)
					.Text(this, &SBlocksUploadWindow::GetDescriptionText)
					.OnTextCommitted(this, &SBlocksUploadWindow::OnDescriptionTextCommitted)
				]
			]
		]
		// The upload button
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			[
				SNew(SButton)
				.IsEnabled(this, &SBlocksUploadWindow::CanUpload)
				.OnClicked(this, &SBlocksUploadWindow::OnUploadClicked)
				.HAlign(HAlign_Center)
				.Content()
				[
					SNew(STextBlock)
					.Justification(ETextJustify::Center)
					.MinDesiredWidth(100)
					.Text(LOCTEXT("Blocks_Upload", "Upload"))
				]
			]
		]
		// Upload progress bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5)
		[
			SNew(SProgressBar)
			.Visibility_Lambda([this]() { return UploadProgress < 0.0f ? EVisibility::Collapsed : EVisibility::Visible; })
			.FillColorAndOpacity(FSlateColor(FLinearColor(0.0f, 1.0f, 1.0f)))
			.Percent_Lambda([this]() { return UploadProgress; })
		]
		// The error message
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ErrorReporting.EmptyBox"))
			.BorderBackgroundColor(FCoreStyle::Get().GetColor("ErrorReporting.BackgroundColor"))
			.ForegroundColor(FCoreStyle::Get().GetColor("ErrorReporting.ForegroundColor"))
			.Visibility_Lambda([this]() { return UploadErrorMessage.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(10)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text_Lambda([this]() { return UploadErrorMessage; })
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]
		]
		// Upload result
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5, 5, 20, 5)
//		.HAlign(HAlign_Center)
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)
			.Visibility_Lambda([this]() { return UploadedImageUrl.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			+SWrapBox::Slot()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(LOCTEXT("UploadCompleted", "Upload successful. You can view your Block at "))
			]
			+SWrapBox::Slot()
			[
				SNew(SHyperlink)
				.Text(this, &SBlocksUploadWindow::GetDisplayUrl)
				.ToolTipText(LOCTEXT("FullEditorToolTip", "This opens the uploaded image in browser."))
				.OnNavigate_Lambda([this]() { FPlatformProcess::LaunchURL(*UploadedImageUrl, nullptr, nullptr); })
			]
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FReply SBlocksUploadWindow::OnLoginClicked()
{
	switch (BlocksInterface->GetLoginState())
	{
	case FLookingGlassBlocksState::STATE_Uninitialized:
	case FLookingGlassBlocksState::STATE_AuthError:
		BlocksInterface->InitiateAuthentication();
		break;
	case FLookingGlassBlocksState::STATE_Authenticated:
		BlocksInterface->Logout();
		break;
	default:
		BlocksInterface->CancelAllRequests();
		break;
	}
	return FReply::Handled();
}

FReply SBlocksUploadWindow::OnTakeScreenshotClicked()
{
	TSharedPtr<SLookingGlassViewport> Viewport = ILookingGlassRuntime::Get().GetLookingGlassViewport();
	if (Viewport.IsValid())
	{
		TSharedRef<FLookingGlassViewportClient> ViewportClient = Viewport->GetLookingGlassViewportClient();

		// Can't pass 'this' as a TSharedPtr/TWeakPtr, Slate will crash with it for some reason.
		// So we're using global BlocksWindowContent variable to check if this object is still alive
		// (e.g. to avoid the situation when clicked at 'screenshot' button and quickly closed the window)
		ViewportClient->TakeQuiltScreenshot(
			[](const FString& Filename)
			{
				if (BlocksWindowContent.IsValid())
				{
					BlocksWindowContent->SetImageFilePath(Filename);
				}
			});
	}

	return FReply::Handled();
}

FReply SBlocksUploadWindow::OnUploadClicked()
{
	// Clear any previous error message, and start the progress bar
	UploadProgress = 0.0f;
	UploadErrorMessage = FText();
	UploadedImageUrl = TEXT("");

	BlocksInterface->UploadImage(ImageFilePath, ImageTitle, ImageDescription,
		// Progress callback
		[](float Progress)
		{
			if (BlocksWindowContent.IsValid())
			{
				BlocksWindowContent->UploadProgress = Progress;
			}
		},
		// Completion callback
		[](const FLookingGlassBlocksUploadResult& Result)
		{
			if (BlocksWindowContent.IsValid())
			{
				BlocksWindowContent->OnUploadCompleted(Result);
			}
		});
	return FReply::Handled();
}

void SBlocksUploadWindow::OnUploadCompleted(const FLookingGlassBlocksUploadResult& Result)
{
	// Store the error message
	UploadErrorMessage = FText::FromString(Result.ErrorMessage);
	// Clear the progress indicator
	UploadProgress = -1.0f;
	// Store the image URL
	UploadedImageUrl = Result.ImageUrl;
}

FText SBlocksUploadWindow::GetLoginButtonText() const
{
	switch (BlocksInterface->GetLoginState())
	{
	case FLookingGlassBlocksState::STATE_Uninitialized:
	case FLookingGlassBlocksState::STATE_AuthError:
		return LOCTEXT("Login", "Login");
	case FLookingGlassBlocksState::STATE_Authenticated:
		return LOCTEXT("Logout", "Logout");
	default:
		return LOCTEXT("Abort", "Abort");
	}
}

FText SBlocksUploadWindow::GetLoginPromptText() const
{
	switch (BlocksInterface->GetLoginState())
	{
	case FLookingGlassBlocksState::STATE_Uninitialized:
		return LOCTEXT("LoginPrompt", "You're not logged in to Blocks");
	case FLookingGlassBlocksState::STATE_AuthError:
		return LOCTEXT("AuthError", "Authentication error!");
	case FLookingGlassBlocksState::STATE_Authenticated:
		{
			// Build the welcome message
			FString Message = LOCTEXT("LoggedIn", "Welcome to Blocks").ToString();
			const FString& UserName = BlocksInterface->GetUserName();
			if (UserName.IsEmpty())
			{
				Message += TEXT("!");
			}
			else
			{
				Message += TEXT(", ") + UserName + TEXT("!");
			}
			return FText::FromString(Message);
		}
	default:
		{
			// Build the logging in message
			FString Message = LOCTEXT("LoggingIn", "Logging in").ToString();
			const FString& DeviceCode = BlocksInterface->GetAuthUserCode();
			if (DeviceCode.IsEmpty())
			{
				Message += TEXT("...");
			}
			else
			{
				Message += TEXT(" [ 4") + DeviceCode + TEXT(" ]");
			}
			return FText::FromString(Message);
		}
	}
}

bool SBlocksUploadWindow::IsLoggedIn() const
{
	return BlocksInterface->GetLoginState() == FLookingGlassBlocksState::STATE_Authenticated;
}

bool SBlocksUploadWindow::CanUpload() const
{
	if (!IsLoggedIn())
	{
		return false;
	}
	if (ImageFilePath.IsEmpty())
	{
		return false;
	}
	if (UploadProgress >= 0.0f)
	{
		return false;
	}
	return true;
}

bool SBlocksUploadWindow::CanTakeScreenshot() const
{
	return ILookingGlassRuntime::Get().IsPlaying();
}

FString SBlocksUploadWindow::GetImageFilePath() const
{
	return ImageFilePath;
}

void SBlocksUploadWindow::SetImageFilePath(const FString& PickedPath)
{
	ImageFilePath = PickedPath;
}

FText SBlocksUploadWindow::GetDescriptionText() const
{
	return FText::FromString(ImageDescription);
}

void SBlocksUploadWindow::OnDescriptionTextCommitted(const FText& Text, ETextCommit::Type CommitInfo)
{
	ImageDescription = Text.ToString();
}

FText SBlocksUploadWindow::GetTitleText() const
{
	return FText::FromString(ImageTitle);
}

void SBlocksUploadWindow::OnTitleTextCommitted(const FText& Text, ETextCommit::Type CommitInfo)
{
	ImageTitle = Text.ToString();
}

FText SBlocksUploadWindow::GetDisplayUrl() const
{
	int32 StartPos = -1;
	FString DisplayUrl;
	StartPos = UploadedImageUrl.Find(TEXT("://"));
	if (StartPos >= 0)
	{
		DisplayUrl = UploadedImageUrl.RightChop(StartPos + 3);
	}
	else
	{
		DisplayUrl = UploadedImageUrl;
	}
	return FText::FromString(DisplayUrl);
}

/*static*/ void SBlocksUploadWindow::ShowWindow(FLookingGlassBlocksInterface* BlocksInterface)
{
	if (BlocksWindowPtr.IsValid())
	{
		BlocksWindowPtr->BringToFront();
		return;
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Looking Glass Blocks Upload Tool"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(500, 280));

	Window->SetContent
	(
		SAssignNew(BlocksWindowContent, SBlocksUploadWindow, Window)
		.WidgetWindow(Window)
		.BlocksInterface(BlocksInterface)
	);

	IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
	TSharedPtr<SWindow> MainFrameParentWindow = MainFrame.GetParentWindow();
	FSlateApplication::Get().AddWindowAsNativeChild(Window, MainFrameParentWindow.ToSharedRef());

	BlocksWindowPtr = Window;
}

void SBlocksUploadWindow::OnWindowClosed(const TSharedRef<SWindow>& Window)
{
	// Stop any polling in interface
	BlocksInterface->CancelAllRequests();

	// Destroy the window
	BlocksWindowPtr.Reset();
	BlocksWindowContent.Reset();
}

#undef LOCTEXT_NAMESPACE
