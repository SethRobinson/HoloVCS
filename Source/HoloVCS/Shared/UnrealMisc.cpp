#include "UnrealMisc.h"


#ifdef WINAPI
//#include <windows.h>

#endif

using namespace std;

string GetFileNameFromString(const string& path)
{
	if (path.empty()) return "";

	for (size_t i = path.size() - 1; i > 0; i--)
	{
		if (path[i] == '/' || path[i] == '\\')
		{
			//well, this must be the cutoff point
			return path.substr(i + 1, path.size() - i);
		}
	}
	return path;
}

string GetFileNameWithoutExtension(const string fileName)
{
	string fName = GetFileNameFromString(fileName);

	size_t dotIndex = fName.find_first_of('.');
	if (dotIndex == string::npos) return fName;
	return fName.substr(0, dotIndex);

}

string GetPathFromString(const string& path)
{

	for (size_t i = path.size() - 1; i > 0; i--)
	{
		if (path[i] == '/' || path[i] == '\\')
		{
			//well, this must be the cutoff point
			return path.substr(0, i + 1);
		}
	}
	return path;
}

//send the desired new extension without the peroid, like "zip", not ".zip"
string ModifyFileExtension(const string fileName, const string extension)
{
	size_t index = fileName.find_last_of('.');
	if (index == string::npos)
	{
		assert(!"Well, it doesn't have an extension to begin with");
		return fileName;
	}

	return fileName.substr(0, index + 1) + extension;
}

string GetFileExtension(string fileName)
{
	size_t index = fileName.find_last_of('.');
	if (index == string::npos)
	{
		return "";
	}

	return fileName.substr(index + 1, fileName.length());
}


void AppendStringToFile(const string filename, const string text)
{
	FILE* fp = NULL;

	/*if (GetPlatformID() == PLATFORM_ID_LINUX)
	{
		fp = fopen(filename.c_str(), "a+");

	}
	else*/
	{
		fp = fopen(filename.c_str(), "ab");

		if (!fp)
		{
			fp = fopen(filename.c_str(), "wb");
		}
	}

	if (!fp)
	{
		//Uhh.... bad idea, could create infinite loop
		//LogError("Unable to create/append to %s", text);
		return;
	}

	fwrite(text.c_str(), text.size(), 1, fp);

	fclose(fp);
}

/*
void LogMsg(const char* traceStr, ...)
{
	va_list argsVA;
	const int logSize = 4096;
	char buffer[logSize];
	memset((void*)buffer, 0, logSize);

	va_start(argsVA, traceStr);
	vsnprintf(buffer, logSize, traceStr, argsVA);
	va_end(argsVA);

#ifdef WINAPI
	OutputDebugString(buffer);
	OutputDebugString("\n");
	printf(buffer);
	printf("\n");
#endif
	//__Linux_log_write(Linux_LOG_ERROR,GetAppName(), buffer);
	g_logCache.m_lines.push_back("LOG: " + GetDateAndTimeAsString() + ": " + string(buffer));

	UpdateLogCache();

#ifndef _CONSOLE
	if (IsBaseAppInitted())
	{
		GetBaseApp()->GetConsole()->AddLine(buffer);
	}
#endif
}
*/


void LogMsg(const char* traceStr, ...)
{
	va_list argsVA;
	const int logSize = 4096;
	char buffer[logSize];
	memset((void*)buffer, 0, logSize);

	va_start(argsVA, traceStr);
	vsnprintf(buffer, logSize, traceStr, argsVA);
	va_end(argsVA);

#ifdef WINAPI
	//OutputDebugString((LPCWSTR)buffer);
	//OutputDebugString((LPCWSTR)"\n");
#endif

	UE_LOG(LogTemp, Display, TEXT("%s"), ANSI_TO_TCHAR(buffer));

	//Hack to aLso write out to our own logfile as shipping builds won't do it by default, assuming release dir structure here on Windows which is bad...
	
	AppendStringToFile("..\\..\\..\\log.txt",( string(buffer)+"\r\n").c_str());


}

void LogMsg(WIDECHAR* traceStr, ...)
{
	va_list argsVA;
	const int logSize = 4096;
	WIDECHAR buffer[logSize];
	memset((void*)buffer, 0, logSize * sizeof(WIDECHAR));

	va_start(argsVA, traceStr);
	vswprintf(buffer, logSize, traceStr, argsVA);
	va_end(argsVA);

#ifdef WINAPI
	//OutputDebugStringW((LPCWSTR)buffer);
	//OutputDebugStringW((LPCWSTR)"\n");
#endif

	UE_LOG(LogTemp, Warning, TEXT("W: %s"), buffer);

}

UActorComponent* GetComponentByName(const AActor* pRootActor, const FString& name)
{
	if (!pRootActor) return NULL;

	TArray<UActorComponent*> children;
	
	//old way, but deprecated
	//children = pRootActor->GetComponentsByClass(UActorComponent::StaticClass());	
	pRootActor->GetComponents(children, true);
	

	for (int i = 0; i < children.Num(); i++)
	{
		//LogMsg(TEXT("Name: %s"), *children[i]->GetName());
		if (children[i]->GetName() == name) return children[i];
	}

	//LogMsg("Found %d components total.", children.Num());

	//couldn't find it
	return NULL;
}

UActorComponent* GetComponentByName(const AActor* pRootActor, const char* name)
{
	const FString ftemp(name);
	return GetComponentByName(pRootActor, ftemp);
}


//returns first actor we happen to see with this tag
AActor* GetActorByTag(UWorld* pWorld, char* tag)
{

	if (!pWorld)
	{
		LogMsg("GetActorByName:  World is null?!");
		return NULL;
	}
	TActorIterator<AActor> Iterator(pWorld, AActor::StaticClass());
	for (; Iterator; ++Iterator)
	{
		AActor* pActor = Cast<AActor>(*Iterator);
		if (pActor->ActorHasTag(tag))
		{
			return pActor;
		}
		//GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Yellow, Actor->GetFName().ToString());  //Print name of the Actor
		 //another code
	}

	return NULL;
}


//Adds all actors with a certain tag to a passed in list
void AddActorsByTag(TArray<AActor*> *pActors, UWorld* pWorld, char* tag)
{
	if (!pWorld)
	{
		LogMsg("GetActorByName:  World is null?!");
		return;
	}
	TActorIterator<AActor> Iterator(pWorld, AActor::StaticClass());
	for (; Iterator; ++Iterator)
	{
		AActor* pActor = Cast<AActor>(*Iterator);
		if (pActor->ActorHasTag(tag))
		{
			pActors->Add(pActor);
		}
	}
}

//Warning:  Don't use this!  Names are actually IDs and weird, and can change!  There is apparently no safe way to get
//the names shown in the editor, in a release build.  So use GetActorByTag instead I guess, and make sure you add a
//tag in the actor section, not object

AActor* GetActorByName(UWorld *pWorld, char *name)
{
	
	if (!pWorld)
	{
		LogMsg("GetActorByName:  World is null?!");
		return NULL;
	}
	TActorIterator<AActor> Iterator(pWorld, AActor::StaticClass());
	for (; Iterator; ++Iterator)
	{
		AActor* pActor = Cast<AActor>(*Iterator);
		if (pActor->GetName() == name)
		{
			return pActor;
		}
		//GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Yellow, Actor->GetFName().ToString());  //Print name of the Actor
		 //another code
	}

	return NULL;
}
