#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Shared/UnrealMisc.h"
#include <list>

//  Programmer(s):  Seth A. Robinson (seth@rtsoft.com)
// 
//This adds a USynthComponentRTAudioBuffer component that can be added.  From there, you can use 
//GetBufferGenerator()->AddChunkScheduled to add an RTSampleChunk.
//Keep adding chunks, and the audio engine will keep trying to feed them to unreal to stream.

//I used the concept of chunks that are read sequentially instead of a more performant approach like a static
//cycling buffer because conceptually simpler and easy to debug. a new/delete/copy per frame isn't a big deal.

//Oh, the reason we're new'ing chunks, sending them in a weird scheduled way and letting the "RTBufferGenerator"
//delete them is because the audio is in a different thread so we have to worry about threadsafety.

//For more info on how the whole unreal audio component thing works (and to learn how to apply filters and things) check
//out "Making a UE Plugin for Audio From Scratch" here: https://forums.unrealengine.com/t/making-a-ue-plugin-for-audio-from-scratch/152606

//Ok, this is kind of confusing, but UE4's original SynthComponent::Start() function does NOT respect the samplerate you've set.  I've fixed it by making a copy with
//it fixed called SynthComponentSethHack (it allows an optional int parm which passes it on to its Initialize)
//Without that, you'll get compile errors, but they are easy enough to work around, just don't pass an int.  You won't be able to change the sample speed on the fly
//though.
//Oh, and you'll need to change all instances of SynthComponentSethHack to SynthComponent.

#include "SynthComponentSethHack.h"

//add includes above here
#include "RTAudioBuffer.generated.h"

using namespace std;

//a thing to help pass audio data in a threadsafe way
class RTSampleChunk
{
public:

    float* pSampleData = NULL;  //you new it, we'll delete it
    int validSamples = 0; //can be less than the actual buffer size, that's ok
    int curPos = 0; //where we are in reading this chunk.  When we get to the end we're done with it
};

class RTBufferGenerator : public ISoundGenerator
{
public:

    RTBufferGenerator(float InSampleRate, int32 InNumChannels, float InVolume);
    virtual ~RTBufferGenerator();

    //~ Begin FSoundGenerator
    virtual int32 GetNumChannels() { return NumChannels; };
    virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
    //~ End FSoundGenerator

    void SetVolume(float InVolume);
    void AddChunkSchedule(RTSampleChunk chunk);
    int m_samplesInBuffer = 0;

private:

    void AddChunk(RTSampleChunk chunk); //it's not thread safe
    int CalculateSamplesInBuffer();

    int32 NumChannels = 2;
    float Volume = 1.0f;
    float m_sampleRate = 0;
    float m_lastSampleWritten = 0;
    list< RTSampleChunk> m_chunkList;
};

UCLASS(ClassGroup = Synth, meta = (BlueprintSpawnableComponent))
class HOLOVCS_API USynthComponentRTAudioBuffer : public USynthComponentSethHack
{
    GENERATED_BODY()

        USynthComponentRTAudioBuffer(const FObjectInitializer& ObjInitializer);
    virtual ~USynthComponentRTAudioBuffer();

public:
   
    // The linear volume of the tone generator.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
        float Volume;

    // Sets the volume of the tone generator
    UFUNCTION(BlueprintCallable, Category = "RT Audio Buffer")
        void SetVolume(float InVolume);

    virtual ISoundGeneratorPtr CreateSoundGenerator(int32 InSampleRate, int32 InNumChannels) override;
    //void Start(int sampleRate = -1);

    RTBufferGenerator* GetBufferGenerator() { return (RTBufferGenerator*)m_pRTBufferGenerator.Get(); }

protected:
    // The runtime instance of the sound generator
    ISoundGeneratorPtr m_pRTBufferGenerator;
};