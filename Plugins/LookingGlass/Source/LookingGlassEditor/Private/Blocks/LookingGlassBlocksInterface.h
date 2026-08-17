#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

#include "Runtime/Launch/Resources/Version.h"

class FJsonObject;

// Login state
struct FLookingGlassBlocksState
{
	enum EPhase
	{
		STATE_Uninitialized,
		STATE_AuthError,
		STATE_RequestedToken,
		STATE_Authenticated,
	};

	EPhase Phase;

	FString DeviceCode;

	FString UserCode;

	FString UserIdName;

	FString UserDisplayName;

	FString AuthToken;

	float AuthPollInterval;

	FLookingGlassBlocksState()
		: Phase(STATE_Uninitialized)
		, AuthPollInterval(3.0f)
	{}

	void Logout()
	{
		Phase = STATE_Uninitialized;
		DeviceCode = TEXT("");
		UserCode = TEXT("");
		AuthToken = TEXT("");
		UserIdName = TEXT("");
		UserDisplayName = TEXT("");
	}
};

// The structure holding all of the information regarding upload success/failure
struct FLookingGlassBlocksUploadResult
{
	bool IsFailed() const
	{
		return !ErrorMessage.IsEmpty();
	}

	FString ErrorMessage;

	FString ImageUrl;
};

// The main class with all of the Blocks functionality
class FLookingGlassBlocksInterface
{
public:
	using FRequestCallback = TFunction<void(TSharedPtr<FJsonObject>, const FString&)>;
	using FUploadProgressCallback = TFunction<void(float)>;
	using FUploadedCallback = TFunction<void(const FLookingGlassBlocksUploadResult&)>;

	FLookingGlassBlocksInterface();

	~FLookingGlassBlocksInterface();

	void InitiateAuthentication();

	void Logout();

	void CancelAllRequests();

	FLookingGlassBlocksState::EPhase GetLoginState() const
	{
		return State.Phase;
	}

	const FString& GetAuthUserCode() const
	{
		return State.UserCode;
	}

	const FString& GetUserName() const
	{
		return State.UserDisplayName;
	}

	bool UploadImage(const FString& Filename, const FString& Title, const FString& Description, FUploadProgressCallback ProgressCallback, FUploadedCallback CompletedCallback);

	// Open the Blocks UI
	void OpenBlocksWindow();

private:

	// Basic request stuff

	void SendRequest(const FString& URL, const FString& ContentType, const FString& AuthString, const FString& RequestContent, FRequestCallback Callback);

	void SendOauthRequest(const FString& URL, const FString& RequestContent, FRequestCallback Callback);

	void SendAPIRequestBase(const FString& RequestHeader, const FString& CommandName, const FString& CommandBody, const FString& ParametersJson, bool bIsMutation, FRequestCallback Callback);

	// Send a simple GraphQL request
	void SendAPIRequest(const FString& RequestName, const FString& CommandName, const FString& CommandBody, bool bIsMutation, FRequestCallback Callback);

	// Special requests and handlers

	// Throw an upload error with a message
	void UploadError(const FString& Message);

	// Pre-upload phase: request the URL for image on AWS server
	void GetUploadURL(const FString& Filename);

	// Upload phase
	void StartUpload();

	// Post-upload: register the image on Blocks server
	void CreateHologram();

	void HandleOauthResponse(TSharedPtr<FJsonObject> Response, const FString& ErrorMessage);

	void HandleTokenResponse(TSharedPtr<FJsonObject> Response, const FString& ErrorMessage);

	void WaitAndRequestOauthToken();

	void AuthenticationError(const FString& ErrorMessage);

	void RequestUserName();

	bool bDebugRequests;

	// Http requests state, e.g. login information
	FLookingGlassBlocksState State;

	// Just storing all of the dynamic stuff of uploading in this structure
	TUniquePtr<struct FBlocksUploadState> UploadState;

#if ENGINE_MAJOR_VERSION < 5
	FDelegateHandle TickerHandle;
#else
	FTSTicker::FDelegateHandle TickerHandle;
#endif
};
