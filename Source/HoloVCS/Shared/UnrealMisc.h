#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <cassert>
#include <string>

#include "CoreMinimal.h"
#include "EngineUtils.h"
//To include crazy stuff:  https://docs.unrealengine.com/4.26/en-US/ProductionPipelines/BuildTools/UnrealBuildTool/ThirdPartyLibraries/
#include "Windows/WindowsHWrapper.h"

using namespace std;


#ifndef SAFE_DELETE
#define SAFE_DELETE(p)      { if(p) { delete (p); (p)=NULL; } }
#endif

#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);   (p)=NULL; } }

#ifndef SAFE_FREE
#define SAFE_FREE(p)      { if(p) { free (p); (p)=NULL; } }
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if(p) {(p)->Release(); (p)=NULL; } }
#endif

#define MAKE_RGB(r, g, b)      ( ((uint32)(r) << 8) + ((uint32)(g) << 16) + ((uint32)(b) << 24) )
#define MAKE_RGBA(r, g, b, a) ( ((uint32)(r) << 8) + ((uint32)(g) << 16) + ((uint32)(b) << 24) + ((uint32)(a)))
const uint32 PURE_WHITE = MAKE_RGBA(255, 255, 255, 255);

#define GET_BLUE(p)        ( (p)               >> 24)
#define GET_GREEN(p)          (((p) & 0x00FF0000) >> 16)
#define GET_RED(p)        (((p) & 0x0000FF00) >>  8)
#define GET_ALPHA(p)         ( (p) & 0x000000FF       )

#define DEG2RAD(x) (M_PI * (x) / 180.0)
#define RAD2DEG(x) (x * (180/M_PI))

#ifndef UINT_MAX
//fix problem for webOS compiles
#define UINT_MAX      0xffffffff
#endif


#define rt_min(rangeMin,rangeMax)    (((rangeMin) < (rangeMax)) ? (rangeMin) : (rangeMax))
#define rt_max(rangeMin,rangeMax)            (((rangeMin) > (rangeMax)) ? (rangeMin) : (rangeMax))

void AppendStringToFile(const std::string filename, const std::string text);

//helper to turn anything into a string, like ints/floats
template< class C>
std::string toString(C value)
{
	std::ostringstream o;
	o << value;
	return o.str();
}

template<> inline
std::string toString(FVector2D value)
{
	return std::string("X: ") + toString(value.X) + " Y: " + toString(value.Y);
}

template<> inline
std::string toString(FVector value)
{
	return std::string("X: ") + toString(value.X) + " Y: " + toString(value.Y) + " Z: " + toString(value.Z);
}

template<> inline
std::string toString(TArray<FVector> value)
{
	std::string temp;
	for (int i = 0; i < value.Num(); i++)
	{
		if (temp.empty())
		{
			temp += "\r\n";
		}
		temp += std::string("#") + toString(i) + " " + toString(value[i]) + "\r\n";
	}

	return temp;
}


void LogMsg(const char* traceStr, ...);
void LogMsg(WIDECHAR* traceStr, ...);
string GetFileExtension(string fileName);
string ModifyFileExtension(const string fileName, const string extension);
string GetPathFromString(const string& path);
string GetFileNameWithoutExtension(const string fileName);
string GetFileNameFromString(const string& path);

UActorComponent* GetComponentByName(const AActor* pRootActor, const FString& name);
UActorComponent* GetComponentByName(const AActor* pRootActor, const char* name);
AActor* GetActorByName(UWorld* pWorld, char* name); //don't use this, names change between runs!
AActor* GetActorByTag(UWorld* pWorld, char* tag); //safe, but .. yeah, you need to add an actor tag
void AddActorsByTag(TArray<AActor*>* pActors, UWorld* pWorld, char* tag);