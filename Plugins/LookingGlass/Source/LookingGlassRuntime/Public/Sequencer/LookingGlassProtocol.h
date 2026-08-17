#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MovieSceneCaptureProtocolBase.h"
#include "Async/Future.h"
#include "AVIWriter.h"

#include "LookingGlassSettings.h"

#include "LookingGlassProtocol.generated.h"

class IImageWriteQueue;

struct IFramePayload
{
	virtual ~IFramePayload() {}

	/**
	* Called when the buffer is now available in CPU ram
	* Return true if you would like to execute the default behavior. (If you return false, GetCapturedFrames will be empty).
	*/
	virtual bool OnFrameReady_RenderThread(FColor* ColorBuffer, FIntPoint BufferSize, FIntPoint TargetSize) const { return true; }
};

typedef TSharedPtr<IFramePayload, ESPMode::ThreadSafe> FFramePayloadPtr;

/** Structure representing a captured frame */
struct FCapturedFrameData
{
	FCapturedFrameData(FIntPoint InBufferSize, FFramePayloadPtr InPayload) : BufferSize(InBufferSize), Payload(MoveTemp(InPayload)) {}

	FCapturedFrameData(FCapturedFrameData&& In) : ColorBuffer(MoveTemp(In.ColorBuffer)), BufferSize(In.BufferSize), Payload(MoveTemp(In.Payload)) {}
	FCapturedFrameData& operator=(FCapturedFrameData&& In){ ColorBuffer = MoveTemp(In.ColorBuffer); BufferSize = In.BufferSize; Payload = MoveTemp(In.Payload); return *this; }

	template<typename T>
	T* GetPayload() { return static_cast<T*>(Payload.Get()); }

	/** The color buffer of the captured frame */
	TArray<FColor> ColorBuffer;

	/** The size of the resulting color buffer */
	FIntPoint BufferSize;

	/** Optional user-specified payload */
	FFramePayloadPtr Payload;

private:
	FCapturedFrameData(const FCapturedFrameData& In);
	FCapturedFrameData& operator=(const FCapturedFrameData& In);
};

// Class for allowing MovieCapture to get images from LookingGlass renders

// Reference: UFrameGrabberProtocol. Base class for capture protocol for sequencer.
UCLASS(Abstract, config=EditorPerProjectUserSettings)
class ULookingGlassProtocol : public UMovieSceneImageCaptureProtocolBase
{
	GENERATED_BODY()

public:
	ULookingGlassProtocol(const FObjectInitializer& ObjectInit)
	: Super(ObjectInit)
	{}

protected:

	// Callback from FLookingGlassViewportClient when the new bitmap is ready for being used in capture
	void OnFrameReady(const TArray<FColor>& Bitmap, int32 Width, int32 Height);

#if WITH_EDITOR
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**~ UMovieSceneCaptureProtocolBase Implementation */
	virtual bool HasFinishedProcessingImpl() const override;
	virtual bool SetupImpl() override;
	virtual void CaptureFrameImpl(const FFrameMetrics& FrameMetrics);
	virtual void TickImpl() override;
	virtual void FinalizeImpl() override;
	virtual void OnReleaseConfigImpl(FMovieSceneCaptureSettings& InSettings) override;
	virtual void OnLoadConfigImpl(FMovieSceneCaptureSettings& InSettings) override;
	/**~ End UMovieSceneCaptureProtocolBase Implementation */

	/**
	 * Retrieve an arbitrary set of data that relates to the specified frame metrics.
	 * This data will be passed through the capture pipeline, and will be accessible from ProcessFrame
	 *
	 * @param FrameMetrics	Metrics specific to the current frame
	 * @param Host			The host that is managing this protocol

	 * @return Shared pointer to a payload to associate with the frame, or nullptr
	 */
	virtual FFramePayloadPtr GetFramePayload(const FFrameMetrics& FrameMetrics) PURE_VIRTUAL(UFrameGrabberProtocol::GetFramePayload, return nullptr;)

	/**
	 * Process a captured frame. This may be called on any thread.
	 *
	 * @param Frame			The captured frame data, including any payload retrieved from GetFramePayload
	 */
	virtual void ProcessFrame(FCapturedFrameData Frame) PURE_VIRTUAL(UFrameGrabberProtocol::ProcessFrame)

	/** The total number of frames we are currently waiting on */
	FThreadSafeCounter OutstandingFrameCount;

	/** Only to be accessed from the render thread - array of frame payloads to be captured from the rendered slate window sorted first to last. */
	TArray<FFramePayloadPtr> PendingFramePayloads;

	/** Array of captured frames */
	TArray<FCapturedFrameData> CapturedFrames;

	/** Lock to protect the above array */
	mutable FCriticalSection CapturedFramesMutex;

	// Update the video/image resolution depending on the current TilingSettings
	void UpdateResolutionInUI();

	FCaptureResolution* GetCurrentResolitionSetting();

	// Resolution of the video which was used before switching to the LookingGlass mode in Sequencer
	FCaptureResolution SavedResolution;

public:
	// Resolution and tiling settings of the generated image/video sequence
	UPROPERTY(config, EditAnywhere, Category="LookingGlass")
	ELookingGlassQualitySettings TilingSettings = ELookingGlassQualitySettings::Q_GoPortrait;
};

// Reference: UImageSequenceProtocol + UCompressedImageSequenceProtocol
UCLASS(meta=(DisplayName="LookingGlass Image Sequence (png)"))
class ULookingGlassProtocol_PNG : public ULookingGlassProtocol
{
	GENERATED_BODY()

public:
	ULookingGlassProtocol_PNG(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
	, CompressionQuality(100)
	{}

	/** ~ UMovieSceneCaptureProtocol implementation */
	virtual bool SetupImpl() override;
	virtual void AddFormatMappingsImpl(TMap<FString, FStringFormatArg>& FormatMappings) const override;
	virtual void BeginFinalizeImpl() override;
	virtual void FinalizeImpl() override;
	virtual bool HasFinishedProcessingImpl() const override;
	virtual void OnReleaseConfigImpl(FMovieSceneCaptureSettings& InSettings) override;
	virtual void OnLoadConfigImpl(FMovieSceneCaptureSettings& InSettings) override;
	/** ~End UMovieSceneCaptureProtocol implementation */

	/** ~ULookingGlassProtocol implementation */
	virtual FFramePayloadPtr GetFramePayload(const FFrameMetrics& FrameMetrics) override;
	virtual void ProcessFrame(FCapturedFrameData Frame) override;
	/** ~End ULookingGlassProtocol implementation */

	/** Level of compression to apply to the image, between 1 (worst quality, best compression) and 100 (best quality, worst compression)*/
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=ImageSettings, meta=(ClampMin=1, ClampMax=100))
	int32 CompressionQuality;

private:
	/** A pointer to the image write queue used for asynchronously writing images */
	IImageWriteQueue* ImageWriteQueue;

	/** A future that is created on BeginFinalize from a fence in the image write queue that will be fulfilled when all currently pending tasks have been completed */
	TFuture<void> FinalizeFence;
};

// Reference: UVideoCaptureProtocol
UCLASS(meta=(DisplayName="LookingGlass Video Sequence (avi)"))
class ULookingGlassProtocol_AVI : public ULookingGlassProtocol
{
	GENERATED_BODY()

public:
	ULookingGlassProtocol_AVI(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
	, bUseCompression(true)
	, CompressionQuality(100)
	{}

public:
	/** ~ UMovieSceneCaptureProtocol implementation */
	virtual void FinalizeImpl() override;
	virtual bool CanWriteToFileImpl(const TCHAR* InFilename, bool bOverwriteExisting) const override;
	/** ~End UMovieSceneCaptureProtocol implementation */

	/** ~ULookingGlassProtocol implementation */
	virtual FFramePayloadPtr GetFramePayload(const FFrameMetrics& FrameMetrics) override;
	virtual void ProcessFrame(FCapturedFrameData Frame) override;
	/** ~End ULookingGlassProtocol implementation */

	UPROPERTY(config, EditAnywhere, Category=VideoSettings)
	bool bUseCompression;

	UPROPERTY(config, EditAnywhere, Category=VideoSettings, meta=(ClampMin=1, ClampMax=100, EditCondition=bUseCompression))
	float CompressionQuality;

protected:
	void ConditionallyCreateWriter(int32 Width, int32 Height);

	TArray<TUniquePtr<FAVIWriter>> AVIWriters;
};
