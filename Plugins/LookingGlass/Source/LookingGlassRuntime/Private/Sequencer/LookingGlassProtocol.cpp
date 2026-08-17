#include "Sequencer/LookingGlassProtocol.h"

#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "ImageWriteQueue.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneCaptureModule.h"
#include "MovieSceneCapture.h"
#include "AVIWriter.h"

#include "Render/LookingGlassViewportClient.h"
#include "Game/LookingGlassSceneCaptureComponent2D.h"

struct FImageFrameData : IFramePayload
{
	FString Filename;
};

bool ULookingGlassProtocol::HasFinishedProcessingImpl() const
{
	FScopeLock Lock(&CapturedFramesMutex);
	return OutstandingFrameCount.GetValue() == 0 && CapturedFrames.Num() == 0;
}

bool ULookingGlassProtocol::SetupImpl()
{
	// Add hook for rendered frame, so we'll capture it directly ourselves
	FLookingGlassViewportClient::OnLookingGlassFrameReady.AddUObject(this, &ULookingGlassProtocol::OnFrameReady);

	// Get the current quality
	ELookingGlassQualitySettings TilingQuality = TilingSettings;
	if (TilingQuality == ELookingGlassQualitySettings::Q_Automatic)
	{
		TilingQuality = ULookingGlassSceneCaptureComponent2D::GetAutomaticTilingQuality();
	}
	// Set the global quality for rendering, so switching between components with different setup won't affect the picture
	ULookingGlassSceneCaptureComponent2D::SetGlobalTilingProperties(TilingQuality);

	return true;
}

void ULookingGlassProtocol::CaptureFrameImpl(const FFrameMetrics& FrameMetrics)
{
	OutstandingFrameCount.Increment();
	//todo: in a case OnFrameReady will be called from render thread, should use ENQUEUE_RENDER_COMMAND (see FFrameGrabber::CaptureThisFrame)
	check(IsInGameThread());
	PendingFramePayloads.Add(GetFramePayload(FrameMetrics));
}

void ULookingGlassProtocol::TickImpl()
{
	TArray<FCapturedFrameData> Frames;

	// Get CapturedFrames
	{
		FScopeLock Lock(&CapturedFramesMutex);
		Swap(Frames, CapturedFrames);
		CapturedFrames.Reset();
	}

	// And pass them to ProcessFrame()
	for (FCapturedFrameData& Frame : Frames)
	{
		ProcessFrame(MoveTemp(Frame));
	}
}

void ULookingGlassProtocol::FinalizeImpl()
{
	FLookingGlassViewportClient::OnLookingGlassFrameReady.RemoveAll(this);
	// Unhook global resolution
	ULookingGlassSceneCaptureComponent2D::ResetGlobalTilingProperties();
}

void ULookingGlassProtocol::OnFrameReady(const TArray<FColor>& Bitmap, int32 Width, int32 Height)
{
	if (PendingFramePayloads.Num() == 0)
	{
		// No frames to capture
		return;
	}

	FFramePayloadPtr Payload = PendingFramePayloads[0];
	PendingFramePayloads.RemoveAt(0, 1, EAllowShrinking::Yes);

	FCapturedFrameData FrameData(FIntPoint(Width, Height), Payload);
	FrameData.ColorBuffer = Bitmap;

	// Store data
	FScopeLock Lock(&CapturedFramesMutex);
	CapturedFrames.Add(MoveTemp(FrameData));
	// This frame is no longer "outstanding"
	OutstandingFrameCount.Decrement();
}

void ULookingGlassProtocol::OnLoadConfigImpl(FMovieSceneCaptureSettings& InSettings)
{
	// Save resolution setting
	FCaptureResolution* pResolution = GetCurrentResolitionSetting();
	if (pResolution)
	{
		SavedResolution = *pResolution;
	}
	UpdateResolutionInUI();

	Super::OnLoadConfigImpl(InSettings);
}

void ULookingGlassProtocol::OnReleaseConfigImpl(FMovieSceneCaptureSettings& InSettings)
{
	// Restore saved resolution setting. Note: this function is not called when Sequencer UI is simply closed
	FCaptureResolution* pResolution = GetCurrentResolitionSetting();
	if (pResolution)
	{
		*pResolution = SavedResolution;
	}

	Super::OnLoadConfigImpl(InSettings);
}

FCaptureResolution* ULookingGlassProtocol::GetCurrentResolitionSetting()
{
	// Resolution settings are stored in UMovieSceneCapture's Settings object, which is outer of this class
	UObject* ParentObject = GetOuter();
	UMovieSceneCapture* Capture = Cast<UMovieSceneCapture>(GetOuter());
	if (Capture == nullptr)
	{
		return nullptr;
	}

	return &Capture->Settings.Resolution;
}

#if WITH_EDITOR
void ULookingGlassProtocol::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		FName PropertyName = PropertyChangedEvent.Property->GetFName();
		// Update the resolution when changing the tiling settings
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULookingGlassProtocol, TilingSettings))
		{
			UpdateResolutionInUI();
		}
	}
}
#endif // WITH_EDITOR

void ULookingGlassProtocol::UpdateResolutionInUI()
{
	// Get the current quality
	ELookingGlassQualitySettings TilingQuality = TilingSettings;
	if (TilingQuality == ELookingGlassQualitySettings::Q_Automatic)
	{
		TilingQuality = ULookingGlassSceneCaptureComponent2D::GetAutomaticTilingQuality();
	}

	FCaptureResolution* pResolution = GetCurrentResolitionSetting();
	if (pResolution)
	{
		const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();
		FLookingGlassTilingQuality TilingValues = LookingGlassSettings->GetTilingQualityFor(TilingSettings);

		// Store resolution
		pResolution->ResX = TilingValues.QuiltW;
		pResolution->ResY = TilingValues.QuiltH;
	}
}

void ULookingGlassProtocol_PNG::OnLoadConfigImpl(FMovieSceneCaptureSettings& InSettings)
{
	// Add .{frame} if it doesn't already exist
	FString OutputFormat = InSettings.OutputFormat;

	// Ensure the format string tries to always export a uniquely named frame so the file doesn't overwrite itself if the user doesn't add it.
	bool bHasFrameFormat = OutputFormat.Contains(TEXT("{frame}")) || OutputFormat.Contains(TEXT("{shot_frame}"));
	if (!bHasFrameFormat)
	{
		OutputFormat.Append(TEXT(".{frame}"));
		InSettings.OutputFormat = OutputFormat;

		UE_LOG(LogMovieSceneCapture, Display, TEXT("Automatically appended .{frame} to the format string as specified format string did not provide a way to differentiate between frames via {frame} or {shot_frame}!"));
	}

	Super::OnLoadConfigImpl(InSettings);
}

void ULookingGlassProtocol_PNG::OnReleaseConfigImpl(FMovieSceneCaptureSettings& InSettings)
{
	// Remove .{frame} if it exists. The "." before the {frame} is intentional because some media players denote frame numbers separated by "."
	InSettings.OutputFormat = InSettings.OutputFormat.Replace(TEXT(".{frame}"), TEXT(""));

	Super::OnReleaseConfigImpl(InSettings);
}

bool ULookingGlassProtocol_PNG::SetupImpl()
{
	FParse::Value( FCommandLine::Get(), TEXT( "-MovieQuality=" ), CompressionQuality );
	CompressionQuality = FMath::Clamp<int32>(CompressionQuality, 1, 100);

	ImageWriteQueue = &FModuleManager::Get().LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue").GetWriteQueue();
	FinalizeFence = TFuture<void>();

	return Super::SetupImpl();
}

bool ULookingGlassProtocol_PNG::HasFinishedProcessingImpl() const
{
	return Super::HasFinishedProcessingImpl() && (!FinalizeFence.IsValid() || FinalizeFence.WaitFor(0));
}

void ULookingGlassProtocol_PNG::BeginFinalizeImpl()
{
	FinalizeFence = ImageWriteQueue->CreateFence();
	// note: original implementation (UImageSequenceProtocol::BeginFinalizeImpl) is not calling Super::BeginFinalizeImpl
	Super::BeginFinalizeImpl();
}

void ULookingGlassProtocol_PNG::FinalizeImpl()
{
	if (FinalizeFence.IsValid())
	{
		double StartTime = FPlatformTime::Seconds();

		FScopedSlowTask SlowTask(0, NSLOCTEXT("ImageSequenceProtocol", "Finalizing", "Finalizing write operations..."));
		SlowTask.MakeDialogDelayed(.1f, true, true);

		FTimespan HalfSecond = FTimespan::FromSeconds(0.5);
		while ( !GWarn->ReceivedUserCancel() && !FinalizeFence.WaitFor(HalfSecond) )
		{
			// Tick the slow task
			SlowTask.EnterProgressFrame(0);
		}
	}

	Super::FinalizeImpl();
}

FFramePayloadPtr ULookingGlassProtocol_PNG::GetFramePayload(const FFrameMetrics& FrameMetrics)
{
	TSharedRef<FImageFrameData, ESPMode::ThreadSafe> FrameData = MakeShareable(new FImageFrameData);

#if 0
	const TCHAR* Extension = TEXT("");
	switch(Format)
	{
	case EImageFormat::BMP:		Extension = TEXT(".bmp"); break;
	case EImageFormat::PNG:		Extension = TEXT(".png"); break;
	case EImageFormat::JPEG:	Extension = TEXT(".jpg"); break;
	case EImageFormat::EXR:		Extension = TEXT(".exr"); break;
	}
#else
	const TCHAR* Extension = TEXT(".png");
#endif

	FrameData->Filename = GenerateFilenameImpl(FrameMetrics, Extension);
	EnsureFileWritableImpl(FrameData->Filename);

	return FrameData;
}

void ULookingGlassProtocol_PNG::ProcessFrame(FCapturedFrameData Frame)
{
	check(Frame.ColorBuffer.Num() >= Frame.BufferSize.X * Frame.BufferSize.Y);

	TUniquePtr<FImageWriteTask> ImageTask = MakeUnique<FImageWriteTask>();

	// Move the color buffer into a raw image data container that we can pass to the write queue
	ImageTask->PixelData = MakeUnique<TImagePixelData<FColor>>(Frame.BufferSize, TArray64<FColor>(MoveTemp(Frame.ColorBuffer)));
	//todo: can remove #if 0...#endif later if we won't need any other output image formats
#if 0
	if (Format == EImageFormat::PNG)
#endif
	{
		// Always write full alpha for PNGs
		ImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));
	}

#if 0
	switch (Format)
	{
	case EImageFormat::EXR:
	case EImageFormat::PNG:
	case EImageFormat::BMP:
	case EImageFormat::JPEG:
		ImageTask->Format = Format;
		break;

	default:
		check(false);
		break;
	}
#else
	ImageTask->Format = EImageFormat::PNG;
#endif

	ImageTask->CompressionQuality = CompressionQuality;
	ImageTask->Filename = Frame.GetPayload<FImageFrameData>()->Filename;

	ImageWriteQueue->Enqueue(MoveTemp(ImageTask));
}

void ULookingGlassProtocol_PNG::AddFormatMappingsImpl(TMap<FString, FStringFormatArg>& FormatMappings) const
{
	FormatMappings.Add(TEXT("quality"), CompressionQuality);
}


struct FVideoFrameData : IFramePayload
{
	FFrameMetrics Metrics;
//	int32 WriterIndex;
};

FFramePayloadPtr ULookingGlassProtocol_AVI::GetFramePayload(const FFrameMetrics& FrameMetrics)
{
	// Note: UVideoCaptureProtocol calls ConditionallyCreateWriter in this function, however at this time we don't know
	// the resolution of rendering, so we'll delay it till ProcessFrame() call.
	//ConditionallyCreateWriter();

	TSharedRef<FVideoFrameData, ESPMode::ThreadSafe> FrameData = MakeShareable(new FVideoFrameData);
	FrameData->Metrics = FrameMetrics;
	//FrameData->WriterIndex = AVIWriters.Num() - 1;
	return FrameData;
}

void ULookingGlassProtocol_AVI::ProcessFrame(FCapturedFrameData Frame)
{
	ConditionallyCreateWriter(Frame.BufferSize.X, Frame.BufferSize.Y);
	int32 WriterIndex = AVIWriters.Num() - 1;

	if (WriterIndex >= 0)
	{
		FVideoFrameData* Payload = Frame.GetPayload<FVideoFrameData>();

		AVIWriters[WriterIndex]->DropFrames(Payload->Metrics.NumDroppedFrames);
		AVIWriters[WriterIndex]->Update(Payload->Metrics.TotalElapsedTime, MoveTemp(Frame.ColorBuffer));

		// Finalize previous writers if necessary
		for (int32 Index = 0; Index < WriterIndex; ++Index)
		{
			TUniquePtr<FAVIWriter>& Writer = AVIWriters[Index];
			if (Writer->IsCapturing())
			{
				Writer->Finalize();
			}
		}
	}
}

void ULookingGlassProtocol_AVI::FinalizeImpl()
{
	for (TUniquePtr<FAVIWriter>& Writer : AVIWriters)
	{
		if (Writer->IsCapturing())
		{
			Writer->Finalize();
		}
	}

	AVIWriters.Empty();

	Super::FinalizeImpl();
}

bool ULookingGlassProtocol_AVI::CanWriteToFileImpl(const TCHAR* InFilename, bool bOverwriteExisting) const
{
	// When recording video, if the filename changes (ie due to the shot changing), we create new AVI writers.
	// If we're not overwriting existing filenames we need to check if we're already recording a video of that name,
	// before we can deem whether we can write to a new file (we can always write to a filename we're already writing to)
	if (!bOverwriteExisting)
	{
		for (const TUniquePtr<FAVIWriter>& Writer : AVIWriters)
		{
			if (Writer->Options.OutputFilename == InFilename)
			{
				return true;
			}
		}

		return IFileManager::Get().FileSize(InFilename) == -1;
	}

	return true;
}

void ULookingGlassProtocol_AVI::ConditionallyCreateWriter(int32 Width, int32 Height)
{
	static const TCHAR* Extension = TEXT(".avi");

	FString VideoFilename = GenerateFilenameImpl(FFrameMetrics(), Extension);

	if (AVIWriters.Num() && VideoFilename == AVIWriters.Last()->Options.OutputFilename)
	{
		return;
	}

	EnsureFileWritableImpl(VideoFilename);

	FAVIWriterOptions Options;
	Options.OutputFilename = MoveTemp(VideoFilename);
	Options.CaptureFramerateNumerator = CaptureHost->GetCaptureFrameRate().Numerator;
	Options.CaptureFramerateDenominator = CaptureHost->GetCaptureFrameRate().Denominator;
	Options.bSynchronizeFrames = CaptureHost->GetCaptureStrategy().ShouldSynchronizeFrames();
	// Set the video resolution
	Options.Width = Width;
	Options.Height = Height;

	if (bUseCompression)
	{
		Options.CompressionQuality = CompressionQuality / 100.f;

		float QualityOverride = 100.f;
		if (FParse::Value( FCommandLine::Get(), TEXT( "-MovieQuality=" ), QualityOverride ))
		{
			Options.CompressionQuality = FMath::Clamp(QualityOverride, 1.f, 100.f) / 100.f;
		}

		Options.CompressionQuality = FMath::Clamp<float>(Options.CompressionQuality.GetValue(), 0.f, 1.f);
	}

	AVIWriters.Emplace(FAVIWriter::CreateInstance(Options));
	AVIWriters.Last()->Initialize();
}
