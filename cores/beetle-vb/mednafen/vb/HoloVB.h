
#ifndef  MDFN_ALIGN
#define MDFN_ALIGN(n) __declspec(align(n))

#endif // ! MDFN_ALIGN


#ifdef __cplusplus
extern "C"
{
#endif

    struct Layer3DSlice
    {
        bool m_bUsed;
        bool m_hasRGBAData;
        float m_distanceMod;  //0 is the middle, negative would be closer to the screen
        int32 m_objectType;
        int32 m_width, m_height, m_pitchBytes;
        uint8 m_image[384 * 224 * 4];
    };

    struct Layer3DInfo
    {
        int m_layerCount;
        int m_layersUsed;
        struct Layer3DSlice *m_pLayers;
    };


#ifdef __cplusplus
}
#endif