#include "LookingGlassBlocksInterface.h"
#include "LookingGlassBlocksUI.h" // just for OpenBlocksWindow()

#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"

#include "Http.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Modules/ModuleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"

#include "LookingGlassSettings.h"

// Json helper, for string formatting (EscapeJsonString)
#include "Serialization/JsonWriter.h"


#define OAUTH_CLIENT_ID			TEXT("FxkKrqDolYa0nZdyDXHsbgSxQF85hz0h")
#define OAUTH_HOSTNAME			TEXT("blocks")
#define BLOCKS_API_URL			TEXT("https://blocks.glass")
#define OAUTH_AUDIENCE			BLOCKS_API_URL
#define OAUTH_SCOPE				TEXT("offline_access+openid+profile")

#define TIMEOUT_FOR_API			10.0f
#define TIMEOUT_FOR_UPLOAD		30.0f

// Macros for logging
DEFINE_LOG_CATEGORY_STATIC(LogLookingGlassBlocks, Log, All);
#define LOCTEXT_NAMESPACE "FLookingGlassBlocksInterface"

//#define DEBUG_AUTH_TOKEN		""

FLookingGlassBlocksInterface::FLookingGlassBlocksInterface()
	: bDebugRequests(false)
{
}

FLookingGlassBlocksInterface::~FLookingGlassBlocksInterface()
{
	CancelAllRequests();
}

void FLookingGlassBlocksInterface::SendRequest(const FString& URL, const FString& ContentType, const FString& AuthString, const FString& RequestContent, FRequestCallback Callback)
{
	FHttpModule& HttpModule = FHttpModule::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = HttpModule.CreateRequest();

	// Preparing a POST request
	Request->SetVerb(TEXT("POST"));

	// We'll need to tell the server what type of content to expect in the POST data
	Request->SetHeader(TEXT("Content-Type"), ContentType);

	if (!AuthString.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), AuthString);
	}

	Request->SetContentAsString(RequestContent);

	// Set the timeout for request
	Request->SetTimeout(TIMEOUT_FOR_API);

	if (bDebugRequests)
	{
		UE_LOG(LogLookingGlassBlocks, Warning, TEXT("Request: %s"), *URL);
		UE_LOG(LogLookingGlassBlocks, Warning, TEXT("... Content-Type: %s"), *ContentType);
		UE_LOG(LogLookingGlassBlocks, Warning, TEXT("... Content: [ %s ]"), *RequestContent);
	}

	// Set the http URL
	Request->SetURL(URL);

	// Set the callback, which will execute when the HTTP call is complete
	Request->OnProcessRequestComplete().BindLambda(
		[this, Callback](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			bool bHandled = false;
			FString ErrorMessage;

			if (bConnectedSuccessfully)
			{
				// We should have a JSON response - attempt to process it.
				const FString& ResponseString = Response->GetContentAsString();

				if (bDebugRequests)
				{
					UE_LOG(LogLookingGlassBlocks, Warning, TEXT("Response: [ %s ]"), *ResponseString);
				}

				TSharedPtr<FJsonObject> JsonRoot;
				TSharedRef<TJsonReader<> > Reader = TJsonReaderFactory<>::Create(ResponseString);
				if (!FJsonSerializer::Deserialize(Reader, JsonRoot) || !JsonRoot.IsValid())
				{
					// In some cases, Response string will contain an error message
					ErrorMessage = ResponseString.IsEmpty() ? TEXT("Problem parsing response") : ResponseString;
				}
				else
				{
					Callback(JsonRoot, ErrorMessage);
					bHandled = true;
				}
			}
			else
			{
				// There could be a EHttpRequestStatus::Failed_ConnectionError, could retry the request in this case.
				// Take a look at FHttpRetrySystem.
				if (Request->GetStatus() == EHttpRequestStatus::Failed &&
					Request->GetFailureReason() == EHttpFailureReason::ConnectionError)
				{
					FString HostName = FPlatformHttp::GetUrlDomain(Response->GetURL());
					ErrorMessage = FString::Printf(TEXT("Failed to connect to host %s"), *HostName);
				}
				else
				{
					ErrorMessage = EHttpRequestStatus::ToString(Request->GetStatus());
				}
			}

			if (bDebugRequests && !ErrorMessage.IsEmpty())
			{
				UE_LOG(LogLookingGlassBlocks, Error, TEXT("Error in response: %s"), *ErrorMessage);
			}

			if (!bHandled)
			{
				// Execute callback with error string
				TSharedPtr<FJsonObject> DummyJson;
				Callback(DummyJson, ErrorMessage);
			}
		});

	// Submit the request
	Request->ProcessRequest();
}

void FLookingGlassBlocksInterface::SendOauthRequest(const FString& URL, const FString& RequestContent, FRequestCallback Callback)
{
	SendRequest(
		FString(TEXT("https://")) + OAUTH_HOSTNAME + TEXT(".us.auth0.com") + URL,
		TEXT("application/x-www-form-urlencoded"),
		TEXT(""),
		RequestContent, Callback);
}

void FLookingGlassBlocksInterface::SendAPIRequestBase(const FString& RequestHeader, const FString& CommandName, const FString& CommandBody, const FString& ParametersJson, bool bIsMutation, FRequestCallback Callback)
{
	// Build a request
	FString Query = FString::Printf(TEXT("%s %s { %s }"),
		bIsMutation ? TEXT("mutation") : TEXT("query"),
		*RequestHeader, *CommandBody);

	FString ParamsString;
	if (!ParametersJson.IsEmpty())
	{
		// Add parameters
		ParamsString = FString::Printf(TEXT(", \"variables\": { %s }"), *ParametersJson);
	}

	// Build a Json from request
	FString RequestJson = FString::Printf(TEXT("{ \"query\": \"%s\"%s }"),
		*Query,
		*ParamsString);

	SendRequest(
		FString(BLOCKS_API_URL) + TEXT("/api/graphql"), TEXT("application/json"),
		TEXT("Bearer ") + State.AuthToken,
		RequestJson,
		[=](TSharedPtr<FJsonObject> Response, const FString& ErrorMessage)
		{
			// Response has similar structure for all API calls, dig into it to get what we need
			const TSharedPtr<FJsonObject>* DataRootObject = nullptr;
			if (Response.IsValid() && Response->TryGetObjectField(TEXT("data"), DataRootObject))
			{
				const TSharedPtr<FJsonObject>* ResponseObject;
				// Get the response part for the specified command
				if ((*DataRootObject)->TryGetObjectField(CommandName, ResponseObject))
				{
					// Execute the callback
					Callback(*ResponseObject, ErrorMessage);
					return;
				}
			}
			UE_LOG(LogLookingGlassBlocks, Error, TEXT("Invalid API response for request %s: unable to find result"), *CommandName);
		});
}

void FLookingGlassBlocksInterface::SendAPIRequest(const FString& RequestName, const FString& CommandName, const FString& CommandBody, bool bIsMutation, FRequestCallback Callback)
{
	// We're building this request:
	// [query|mutation] RequestName  <-- 1st parameter
	// {
	//    CommandName {    |- 3rd parameter, CommandName is also 2nd parameter
	//      CommandBody    |
	//   }                 |
	// }
	SendAPIRequestBase(
		RequestName,
		CommandName,
		FString::Printf(TEXT("%s { %s }"), *CommandName, *CommandBody),
		TEXT(""),
		bIsMutation,
		Callback
	);
}

void FLookingGlassBlocksInterface::InitiateAuthentication()
{
	// Cache bDebugBlocksRequests
	bDebugRequests = GetDefault<ULookingGlassSettings>()->LookingGlassEditorSettings.bDebugBlocksRequests;

#ifdef DEBUG_AUTH_TOKEN
	// Just for quick debugging - can store auth token in C++ code
	State.Phase = FLookingGlassBlocksState::EPhase::STATE_Authenticated;
	State.AuthToken = TEXT(DEBUG_AUTH_TOKEN);
	RequestUserName();
	return;
#endif

	FString RequestContent = FString::Printf(TEXT("client_id=%s&audience=%s&scope=%s"),
		OAUTH_CLIENT_ID, OAUTH_AUDIENCE, OAUTH_SCOPE);

	SendOauthRequest(TEXT("/oauth/device/code"), RequestContent,
		[this](TSharedPtr<FJsonObject> Response, const FString& ErrorMessage)
		{
			HandleOauthResponse(Response, ErrorMessage);
		});
}

void FLookingGlassBlocksInterface::Logout()
{
	// Do not do anything with oauth, just drop the login information
	State.Logout();
	CancelAllRequests();
}

void FLookingGlassBlocksInterface::CancelAllRequests()
{
	// Cancel the token request
	if (TickerHandle.IsValid())
	{
#if ENGINE_MAJOR_VERSION < 5
		FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
#else
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
#endif
		TickerHandle.Reset();
	}
	if (State.Phase == FLookingGlassBlocksState::STATE_RequestedToken)
	{
		State.Phase = FLookingGlassBlocksState::STATE_Uninitialized;
	}
}

void FLookingGlassBlocksInterface::AuthenticationError(const FString& ErrorMessage)
{
	UE_LOG(LogLookingGlassBlocks, Error, TEXT("%s"), *ErrorMessage);
	State.Phase = FLookingGlassBlocksState::STATE_AuthError;
}

void FLookingGlassBlocksInterface::HandleOauthResponse(TSharedPtr<FJsonObject> Response, const FString& ErrorMessage)
{
	FString ErrorCode;

	// Firstly, handle possible errors
	if (!ErrorMessage.IsEmpty())
	{
		ErrorCode = ErrorMessage;
	}
	else
	{
		// We have a json, check if there's an error inside
		Response->TryGetStringField(TEXT("error"), ErrorCode);
		FString ErrorDescription;
		if (Response->TryGetStringField(TEXT("error_description"), ErrorCode))
		{
			ErrorCode = FString::Printf(TEXT("%s: %s"), *ErrorCode, *ErrorDescription);
		}
	}

	if (!ErrorCode.IsEmpty())
	{
		AuthenticationError(ErrorCode);
		return;
	}

	// No error at this point, handle the response now.
	// Retrieve the URL for verification and the device code.
	FString LaunchURL;
	if (!Response->TryGetStringField(TEXT("verification_uri_complete"), LaunchURL) ||
		!Response->TryGetStringField(TEXT("device_code"), State.DeviceCode) ||
		!Response->TryGetStringField(TEXT("user_code"), State.UserCode))
	{
		UE_LOG(LogLookingGlassBlocks, Error, TEXT("Invalid oauth response"));
		return;
	}

	// Read the poll interval
	double PollInterval = 3.0;
	Response->TryGetNumberField(TEXT("interval"), PollInterval);
	State.AuthPollInterval = (float)PollInterval;

	FString Error;
	FPlatformProcess::LaunchURL(*LaunchURL, nullptr, &Error);
	if (Error.Len() != 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		return;
	}

	WaitAndRequestOauthToken();
}

void FLookingGlassBlocksInterface::WaitAndRequestOauthToken()
{
	State.Phase = FLookingGlassBlocksState::STATE_RequestedToken;

#if ENGINE_MAJOR_VERSION < 5
	TickerHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
#else
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
#endif
		[this](float Delta) -> bool
		{
			// Send the token request
			FString RequestContent = FString::Printf(
				TEXT("client_id=%s&device_code=%s&&grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code"),
				OAUTH_CLIENT_ID, *State.DeviceCode);
			SendOauthRequest(TEXT("/oauth/token"), RequestContent,
				[this](TSharedPtr<FJsonObject> Response, const FString& ErrorMessage)
				{
					HandleTokenResponse(Response, ErrorMessage);
				});
			// Delete and stop the ticker (reset delegate and 'return false')
			// We have a valid http request now, and its handler may initiate a new timer, but at the monent we
			// should remove the current timer
			TickerHandle.Reset();
			return false;
		}), State.AuthPollInterval);
}

void FLookingGlassBlocksInterface::HandleTokenResponse(TSharedPtr<FJsonObject> Response, const FString& ErrorMessage)
{
	if (!ErrorMessage.IsEmpty())
	{
		AuthenticationError(ErrorMessage);
		return;
	}

	FString ErrorCode;
	Response->TryGetStringField(TEXT("error"), ErrorCode);
	if (ErrorCode == TEXT("authorization_pending"))
	{
		// User still have to authenticate in browser, repeat the request
		WaitAndRequestOauthToken();
		return;
	}

	// Other errors:
	// - "slow_down" = polling too frequently
	// - "access_denied" = user declined the authorization
	if (!ErrorCode.IsEmpty())
	{
		AuthenticationError(ErrorCode);
		return;
	}

	if (!Response->TryGetStringField(TEXT("access_token"), State.AuthToken))
	{
		AuthenticationError(TEXT("Bad oauth response"));
		return;
	}

	State.Phase = FLookingGlassBlocksState::STATE_Authenticated;

	RequestUserName();
}

void FLookingGlassBlocksInterface::RequestUserName()
{
	SendAPIRequest(TEXT("GetUserData"), TEXT("me"), TEXT("id username displayName"), false,
		[this](TSharedPtr<FJsonObject> Response, const FString& ErrorMessage)
		{
			if (!Response->TryGetStringField(TEXT("username"), State.UserIdName))
			{
				State.UserIdName = TEXT("bad_user");
			}
			if (!Response->TryGetStringField(TEXT("displayName"), State.UserDisplayName))
			{
				State.UserDisplayName = TEXT("<InvalidUser>");
			}
		});
}

// Structure used to send quilt parameters to Blocks
struct FCreateQuiltHologramArgs
{
	FString Title;
	FString Description;
	FString ImageUrl;
	int32 Width = 0;
	int32 Height = 0;
	FString Type = TEXT("QUILT");
	int32 FileSize = 0;
	float AspectRatio = 1.0f;
	int32 QuiltCols = 0;
	int32 QuiltRows = 0;
	int32 QuiltTileCount = 0;
	bool bIsPublished = true;

	FString ToJson() const
	{
		FString Result;
		Append(Result, TEXT("title"), Title);
		Append(Result, TEXT("description"), Description);
		Append(Result, TEXT("imageUrl"), ImageUrl);
		Append(Result, TEXT("width"), Width);
		Append(Result, TEXT("height"), Height);
		Append(Result, TEXT("type"), Type);
		Append(Result, TEXT("fileSize"), FileSize);
		Append(Result, TEXT("aspectRatio"), AspectRatio);
		Append(Result, TEXT("quiltCols"), QuiltCols);
		Append(Result, TEXT("quiltRows"), QuiltRows);
		Append(Result, TEXT("quiltTileCount"), QuiltTileCount);
		Append(Result, TEXT("isPublished"), bIsPublished);
		return Result;
	}

private:
	static void Append(FString& Result, const TCHAR* ParamName, const FString& Value)
	{
		if (!Result.IsEmpty())
		{
			Result += TEXT(", ");
		}
		// Call EscapeJsonString() to quote special characters. This also adds leading and trailing quotes.
		Result += FString::Printf(TEXT("\"%s\": %s"), ParamName, *EscapeJsonString(Value));
	}
	static void Append(FString& Result, const TCHAR* ParamName, int32 Value)
	{
		if (!Result.IsEmpty())
		{
			Result += TEXT(", ");
		}
		Result += FString::Printf(TEXT("\"%s\": %d"), ParamName, Value);
	}
	static void Append(FString& Result, const TCHAR* ParamName, bool Value)
	{
		if (!Result.IsEmpty())
		{
			Result += TEXT(", ");
		}
		Result += FString::Printf(TEXT("\"%s\": %s"), ParamName, Value ? TEXT("true") : TEXT("false"));
	}
	static void Append(FString& Result, const TCHAR* ParamName, float Value)
	{
		if (!Result.IsEmpty())
		{
			Result += TEXT(", ");
		}
		Result += FString::Printf(TEXT("\"%s\": %f"), ParamName, Value);
	}
};

struct FBlocksUploadState
{
	// The file name of the image to upload
	FString Filename;

	// Hologram parameters
	FCreateQuiltHologramArgs Args;

	// The callback for upload progress indicator
	FLookingGlassBlocksInterface::FUploadProgressCallback ProgressCallback;

	// The callback which is executed for outer code when the operation is completed or failed
	FLookingGlassBlocksInterface::FUploadedCallback CompletedCallback;
};

void FLookingGlassBlocksInterface::UploadError(const FString& Message)
{
	UE_LOG(LogLookingGlassBlocks, Error, TEXT("Upload error: %s"), *Message);
	if (UploadState.IsValid() && UploadState->CompletedCallback)
	{
		FLookingGlassBlocksUploadResult Result;
		Result.ErrorMessage = Message;
		UploadState->CompletedCallback(Result);
		UploadState.Reset();
	}
}

static bool GetImageDimensions(const FString& Filename, int32& OutImageSize, int32& OutWidth, int32& OutHeight)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	FString FileExtension = FPaths::GetExtension(Filename);

	EImageFormat ImageType = EImageFormat::Invalid;
	if (FCString::Stricmp(*FileExtension, TEXT("png")) == 0)
	{
		ImageType = EImageFormat::PNG;
	}
	else if (FCString::Stricmp(*FileExtension, TEXT("bmp")) == 0)
	{
		ImageType = EImageFormat::BMP;
	}
	else if ( (FCString::Stricmp(*FileExtension, TEXT("jpg")) == 0) || (FCString::Stricmp(*FileExtension, TEXT("jpeg")) == 0) )
	{
		ImageType = EImageFormat::JPEG;
	}
	else
	{
		// Unsupported file type
		UE_LOG(LogLookingGlassBlocks, Error, TEXT("Unrecognized image extension: %s"), *Filename);
		return false;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageType);
	check(ImageWrapper.IsValid());
	TArray<uint8> RawFileData;
	if (!FFileHelper::LoadFileToArray(RawFileData, *Filename))
	{
		UE_LOG(LogLookingGlassBlocks, Error, TEXT("Unable to read image file: %s"), *Filename);
		return false;
	}

	ensure(ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()));
	OutImageSize = RawFileData.Num();
	OutWidth = ImageWrapper->GetWidth();
	OutHeight = ImageWrapper->GetHeight();

	return true;
}

bool FLookingGlassBlocksInterface::UploadImage(const FString& Filename, const FString& Title, const FString& Description, FUploadProgressCallback ProgressCallback, FUploadedCallback CompletedCallback)
{
	// Cache bDebugBlocksRequests
	bDebugRequests = GetDefault<ULookingGlassSettings>()->LookingGlassEditorSettings.bDebugBlocksRequests;

	UploadState = TUniquePtr<FBlocksUploadState>(new FBlocksUploadState());
	UploadState->Filename = Filename;
	UploadState->ProgressCallback = ProgressCallback;
	UploadState->CompletedCallback = CompletedCallback;

	// Decompose the filename
	FCreateQuiltHologramArgs& Args = UploadState->Args;
	Args.Title = Title;
	Args.Description = Description;

	// Verify and decompose the file name
	FString ShortImageName = FPaths::GetBaseFilename(Filename);
	int32 ParamsPos = ShortImageName.Find(TEXT("_qs"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (ParamsPos >= 0)
	{
#if PLATFORM_WINDOWS
		if (swscanf_s(&ShortImageName[ParamsPos], TEXT("_qs%dx%da%f"), &Args.QuiltCols, &Args.QuiltRows, &Args.AspectRatio) != 3)
#else
		if (sscanf(TCHAR_TO_ANSI(*ShortImageName + ParamsPos), "_qs%dx%da%f", &Args.QuiltCols, &Args.QuiltRows, &Args.AspectRatio) != 3)
#endif
		{
			Args.QuiltCols = 0;
		}
	}

	if (Args.QuiltCols == 0)
	{
		UploadError(TEXT("No quilt settings in file name detected"));
		return false;
	}
	Args.QuiltTileCount = Args.QuiltCols * Args.QuiltRows;

	if (Title.IsEmpty())
	{
		UploadError(TEXT("Please fill the Block Title"));
		return false;
	}

	if (!GetImageDimensions(Filename, Args.FileSize, Args.Width, Args.Height))
	{
		UploadError(TEXT("Unable to read image data"));
		return false;
	}
	UE_LOG(LogLookingGlassBlocks, Display, TEXT("Uploading image \"%s\" [%dx%d], %d bytes"), *Filename, Args.Width, Args.Height, Args.FileSize);

	// Now, 'Args' is fully set, except 'ImageUrl' field

	GetUploadURL(FPaths::GetCleanFilename(Filename));

	return true;
}

void FLookingGlassBlocksInterface::GetUploadURL(const FString& Filename)
{
	FString RequestContent = FString::Printf(TEXT("file=%s&uploadMode=PUT"), *Filename);

	SendRequest(
		FString(BLOCKS_API_URL) + TEXT("/api/upload"),
		TEXT("application/x-www-form-urlencoded"),
		TEXT("Bearer ") + State.AuthToken,
		RequestContent,
		[this](TSharedPtr<FJsonObject> Response, const FString& ErrorMessage)
		{
			if (ErrorMessage.IsEmpty())
			{
				FString Url;
				if (Response->TryGetStringField(TEXT("url"), Url))
				{
					if (UploadState.IsValid())
					{
						UploadState->Args.ImageUrl = Url;
						StartUpload();
					}
				}
				else
				{
					UE_LOG(LogLookingGlassBlocks, Error, TEXT("Invalid API response for /api/uploload request"));
				}
			}
			else
			{
				UploadError(ErrorMessage);
			}
		});
}

void FLookingGlassBlocksInterface::StartUpload()
{
	if (UploadState.IsValid())
	{
		FHttpModule& HttpModule = FHttpModule::Get();
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = HttpModule.CreateRequest();

		// Preparing a POST request
		Request->SetVerb(TEXT("PUT"));
		FString ContentType = UploadState->Filename.EndsWith(TEXT(".png")) ? TEXT("image/png") : TEXT("image/jpeg");
		Request->SetHeader(TEXT("Content-Type"), ContentType);
		Request->SetURL(UploadState->Args.ImageUrl);
		Request->SetContentAsStreamedFile(UploadState->Filename);

		// Set the timeout
		Request->SetTimeout(TIMEOUT_FOR_UPLOAD);

		// Setup the completion callback
		Request->OnProcessRequestComplete().BindLambda(
			[this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				//todo: not sure if there's any way to verify the correctness of the upload operation
				if (bConnectedSuccessfully)
				{
					// Invoke the last phase of the uploading process
					CreateHologram();
				}
				else
				{
					// There could be a EHttpRequestStatus::Failed_ConnectionError, could retry the request in this case.
					FString ErrorMessage;
					if (Request->GetStatus() == EHttpRequestStatus::Failed &&
						Request->GetFailureReason() == EHttpFailureReason::ConnectionError)
					{
						FString HostName = FPlatformHttp::GetUrlDomain(Response->GetURL());
						ErrorMessage = FString::Printf(TEXT("Failed to connect to host %s"), *HostName);
					}
					else
					{
						ErrorMessage = EHttpRequestStatus::ToString(Request->GetStatus());
					}
					UploadError(ErrorMessage);
				}
			});

		// Setup the progress callback
		Request->OnRequestProgress64().BindLambda([this](FHttpRequestPtr Request, int32 BytesSent, int32 BytesReceived)
			{
				if (UploadState.IsValid() && UploadState->ProgressCallback)
				{
					UploadState->ProgressCallback((float)BytesSent / UploadState->Args.FileSize);
				}
			});

		// Do the initial progress callback
		if (UploadState->ProgressCallback)
		{
			UploadState->ProgressCallback(0.0f);
		}
		// Start the request
		Request->ProcessRequest();
	}
}

void FLookingGlassBlocksInterface::CreateHologram()
{
	static const FString& RequestHeader = TEXT("CreateQuiltHologram($title: String!, $description: String, $imageUrl: String!, $width: Int!, $height: Int!, $fileSize: Int!, $type: HologramType, $aspectRatio: Float, $quiltCols: Int, $quiltRows: Int, $quiltTileCount: Int, $isPublished: Boolean)");
	static const FString& RequestBody = TEXT("createQuiltHologram(title: $title, description: $description, imageUrl: $imageUrl, width: $width, height: $height, fileSize: $fileSize, type: $type, aspectRatio: $aspectRatio, quiltCols: $quiltCols, quiltRows: $quiltRows, quiltTileCount: $quiltTileCount, isPublished: $isPublished) { id }");

	if (UploadState.IsValid())
	{
		FString RequestParameters = UploadState->Args.ToJson();
		SendAPIRequestBase(RequestHeader, TEXT("createQuiltHologram"), RequestBody, RequestParameters, true,
			[this](TSharedPtr<FJsonObject> Response, const FString& ErrorMessage)
			{
				double ImageId = -1.0;
				Response->TryGetNumberField(TEXT("id"), ImageId);
				if (UploadState.IsValid())
				{
					FLookingGlassBlocksUploadResult Result;
					if (!State.UserIdName.IsEmpty() && ImageId > 0)
					{
						// Build URL for the image
						Result.ImageUrl = FString::Printf(TEXT("%s/%s/%d"), BLOCKS_API_URL, *State.UserIdName, (int32)ImageId);
					}
					UE_LOG(LogLookingGlassBlocks, Display, TEXT("Upload complete, image URL: %s"), *Result.ImageUrl);
					UploadState->CompletedCallback(Result);
					UploadState.Reset();
				}
			});
	}
}

void FLookingGlassBlocksInterface::OpenBlocksWindow()
{
	SBlocksUploadWindow::ShowWindow(this);
}

#undef LOCTEXT_NAMESPACE
