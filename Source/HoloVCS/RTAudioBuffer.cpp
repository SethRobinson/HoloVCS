#include "RTAudioBuffer.h"
#include "Shared/UnrealMisc.h"

RTBufferGenerator::RTBufferGenerator(float InSampleRate, int32 InNumChannels, float InVolume)
{
    NumChannels = InNumChannels;
    Volume = InVolume;
    m_sampleRate = InSampleRate;
}

RTBufferGenerator::~RTBufferGenerator()
{
}

//here we're asked by Unreal to fill up the buffer with this number of samples, let's try

int RTBufferGenerator::CalculateSamplesInBuffer()
{
    int samples = 0;
    list< RTSampleChunk>::iterator itor = m_chunkList.begin();

    for (; itor != m_chunkList.end(); itor++)
    {
        samples += itor->validSamples - itor->curPos;
    }

    return samples;
}

int32 RTBufferGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
   
    int samplesWritten = 0;

    while (samplesWritten < NumSamples && !m_chunkList.empty())
        {
            //insert audio from this chunk
            // LogMsg("Processing chunk...");
            //copy up to the # of the sames requested, if we can
            const int samplesWrittenBeforeChunk = samplesWritten;

            for (int i=m_chunkList.front().curPos; i < m_chunkList.front().validSamples; i++)
            {

                OutAudio[samplesWritten++] = m_chunkList.front().pSampleData[m_chunkList.front().curPos++];
                if (samplesWritten == NumSamples)
                {
                    //we're done!
                    break;
                }
            }
            m_samplesQueued.fetch_sub(samplesWritten - samplesWrittenBeforeChunk, std::memory_order_relaxed);
            if (m_chunkList.front().curPos == m_chunkList.front().validSamples)
            {
                //done with this chunk, get rid of it
                m_lastSampleWritten = m_chunkList.front().pSampleData[m_chunkList.front().validSamples - 1];

                delete[] m_chunkList.front().pSampleData;
                m_chunkList.pop_front();
            }
        }
 

    if (samplesWritten < NumSamples)
    {
        //filling the rest with blanks, if we don't it will gve
        //"Procedural USoundWave is reinitializing even though it is actively generating audio. Sound must be stopped before playing again."
        m_underruns.fetch_add(1, std::memory_order_relaxed);
#if UE_BUILD_DEBUG
       // LogMsg("Buffer underrun - wrote %d samples although %d was requested", samplesWritten, NumSamples);
#endif
        while (samplesWritten < NumSamples)
        {
            int lastSampleWritten = samplesWritten-1;

            if (lastSampleWritten >= 0)
            {
                OutAudio[samplesWritten++] = OutAudio[lastSampleWritten]; //repeat last thing
            }
            else
            {
                OutAudio[samplesWritten++] = m_lastSampleWritten;

            }
        }
      
    }

    m_samplesInBuffer = CalculateSamplesInBuffer();

    return samplesWritten;

    /*
    //a function, a simple thing to fill with noise instead
    check(NumChannels > 0);
    int32 NumFrames = NumSamples / NumChannels;
    int32 SampleIndex = 0;
    
    for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
    {
      //  float SampleValue = Volume * m_pRTBufferGenerator.NextSample();
        for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            //OutAudio[SampleIndex++] = SampleValue;
            OutAudio[SampleIndex++] = FMath::RandRange(-1.0f, 1.0f);
        }
    }
    return NumSamples;
    */
    
}

void RTBufferGenerator::AddChunk(RTSampleChunk chunk)
{
    m_chunkList.push_back(chunk);

    //LogMsg("Pushing chunk, we now have %d", m_chunkList.size());
}

//thread safe way, will actually happen right before processing audio callbacks, which I guess will be something like 10 fps, it won't
//match the actual framerate, it just depends on if the audio buffer needs filling or not(?)
void RTBufferGenerator::AddChunkSchedule(RTSampleChunk chunk)
{
    m_samplesQueued.fetch_add(chunk.validSamples, std::memory_order_relaxed);
    SynthCommand([this, chunk]()
        {
            AddChunk(chunk);
        });
}


void RTBufferGenerator::SetVolume(float InVolume)
{
    SynthCommand([this, InVolume]()
        {
            // this will zipper, but this is a hello-world style synth
            Volume = InVolume;
        });
}


//***********************


USynthComponentRTAudioBuffer::USynthComponentRTAudioBuffer(const FObjectInitializer& ObjInitializer)
    : Super(ObjInitializer)
{
}

USynthComponentRTAudioBuffer::~USynthComponentRTAudioBuffer()
{
}
bool USynthComponentRTAudioBuffer::Init(int32& SampleRate)
{
    // Try to set our desired sample rate
    SampleRate = FMath::RoundToInt(DesiredSampleRate);

    // Call the base class Init
    return Super::Init(SampleRate);
}

void USynthComponentRTAudioBuffer::SetDesiredSampleRate(float NewSampleRate)
{
    DesiredSampleRate = NewSampleRate;
    // If already initialized, we may need to reinitialize
    //if (IsInitialized())
    {
        Stop();
        Start();
    }
}

void USynthComponentRTAudioBuffer::SetSampleRate(float NewSampleRate)
{
    DesiredSampleRate = NewSampleRate;
    
}

void USynthComponentRTAudioBuffer::SetVolume(float InVolume)
{
    if (m_pRTBufferGenerator.IsValid())
    {
        RTBufferGenerator* pBuffGen = static_cast<RTBufferGenerator*>(m_pRTBufferGenerator.Get());
        pBuffGen->SetVolume(InVolume);
    }
    else
    {
        LogMsg("Ignoring SetVolume RTBuffer, it isn't valid yet");
    }
}



ISoundGeneratorPtr USynthComponentRTAudioBuffer::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
    LogMsg("Setting up audio buffer.  Sample rate: %d, channels: %d", InParams.SampleRate, InParams.NumChannels);

    // Corrected the creation of the sound generator using parameters from InParams
    m_pRTBufferGenerator = MakeShared<RTBufferGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, Volume);

    return m_pRTBufferGenerator;
}


/*
ISoundGeneratorPtr USynthComponentRTAudioBuffer::CreateSoundGenerator(int32 InSampleRate, int32 InNumChannels)
{
    LogMsg("Setting up audio buffer.  Sample rate: %d, channels: %d", InSampleRate, InNumChannels);
    return m_pRTBufferGenerator = ISoundGeneratorPtr(new RTBufferGenerator(InSampleRate, InNumChannels, Volume));
}
*/

