#ifndef __VIC_DRV_H
#define __VIC_DRV_H

typedef struct VicPipeConfig {
    unsigned int   DownsampleHoriz       : 11;
    unsigned int   reserved0             :  5;
    unsigned int   DownsampleVert        : 11;
    unsigned int   reserved1             :  5;
    unsigned int   reserved2             : 32;
    unsigned int   reserved3             : 32;
    unsigned int   reserved4             : 32;
} VicPipeConfig;

typedef struct VicOutputConfig {
    unsigned long  AlphaFillMode         :  3;
    unsigned long  AlphaFillSlot         :  3;
    unsigned long  BackgroundAlpha       : 10;
    unsigned long  BackgroundR           : 10;
    unsigned long  BackgroundG           : 10;
    unsigned long  BackgroundB           : 10;
    unsigned long  RegammaMode           :  2;
    unsigned long  OutputFlipX           :  1;
    unsigned long  OutputFlipY           :  1;
    unsigned long  OutputTranspose       :  1;
    unsigned long  reserved1             :  1;
    unsigned long  reserved2             : 12;
    unsigned int   TargetRectLeft        : 14;
    unsigned int   reserved3             :  2;
    unsigned int   TargetRectRight       : 14;
    unsigned int   reserved4             :  2;
    unsigned int   TargetRectTop         : 14;
    unsigned int   reserved5             :  2;
    unsigned int   TargetRectBottom      : 14;
    unsigned int   reserved6             :  2;
} VicOutputConfig;

typedef struct VicOutputSurfaceConfig {
    unsigned int   OutPixelFormat        :  7;
    unsigned int   OutChromaLocHoriz     :  2;
    unsigned int   OutChromaLocVert      :  2;
    unsigned int   OutBlkKind            :  4;
    unsigned int   OutBlkHeight          :  4;
    unsigned int   reserved0             :  3;
    unsigned int   reserved1             : 10;
    unsigned int   OutSurfaceWidth       : 14;
    unsigned int   OutSurfaceHeight      : 14;
    unsigned int   reserved2             :  4;
    unsigned int   OutLumaWidth          : 14;
    unsigned int   OutLumaHeight         : 14;
    unsigned int   reserved3             :  4;
    unsigned int   OutChromaWidth        : 14;
    unsigned int   OutChromaHeight       : 14;
    unsigned int   reserved4             :  4;
} VicOutputSurfaceConfig;

typedef struct VicMatrixStruct {
    unsigned long  matrix_coeff00        : 20;
    unsigned long  matrix_coeff10        : 20;
    unsigned long  matrix_coeff20        : 20;
    unsigned long  matrix_r_shift        :  4;
    unsigned long  matrix_coeff01        : 20;
    unsigned long  matrix_coeff11        : 20;
    unsigned long  matrix_coeff21        : 20;
    unsigned long  reserved0             :  3;
    unsigned long  matrix_enable         :  1;
    unsigned long  matrix_coeff02        : 20;
    unsigned long  matrix_coeff12        : 20;
    unsigned long  matrix_coeff22        : 20;
    unsigned long  reserved1             :  4;
    unsigned long  matrix_coeff03        : 20;
    unsigned long  matrix_coeff13        : 20;
    unsigned long  matrix_coeff23        : 20;
    unsigned long  reserved2             :  4;
} VicMatrixStruct;

typedef struct VicClearRectStruct {
    unsigned int   ClearRect0Left        : 14;
    unsigned int   reserved0             :  2;
    unsigned int   ClearRect0Right       : 14;
    unsigned int   reserved1             :  2;
    unsigned int   ClearRect0Top         : 14;
    unsigned int   reserved2             :  2;
    unsigned int   ClearRect0Bottom      : 14;
    unsigned int   reserved3             :  2;
    unsigned int   ClearRect1Left        : 14;
    unsigned int   reserved4             :  2;
    unsigned int   ClearRect1Right       : 14;
    unsigned int   reserved5             :  2;
    unsigned int   ClearRect1Top         : 14;
    unsigned int   reserved6             :  2;
    unsigned int   ClearRect1Bottom      : 14;
    unsigned int   reserved7             :  2;
} VicClearRectStruct;

typedef struct VicSlotStructSlotConfig {
    unsigned long  SlotEnable            :  1;
    unsigned long  DeNoise               :  1;
    unsigned long  AdvancedDenoise       :  1;
    unsigned long  CadenceDetect         :  1;
    unsigned long  MotionMap             :  1;
    unsigned long  MMapCombine           :  1;
    unsigned long  IsEven                :  1;
    unsigned long  ChromaEven            :  1;
    unsigned long  CurrentFieldEnable    :  1;
    unsigned long  PrevFieldEnable       :  1;
    unsigned long  NextFieldEnable       :  1;
    unsigned long  NextNrFieldEnable     :  1;
    unsigned long  CurMotionFieldEnable  :  1;
    unsigned long  PrevMotionFieldEnable :  1;
    unsigned long  PpMotionFieldEnable   :  1;
    unsigned long  CombMotionFieldEnable :  1;
    unsigned long  FrameFormat           :  4;
    unsigned long  FilterLengthY         :  2;
    unsigned long  FilterLengthX         :  2;
    unsigned long  Panoramic             : 12;
    unsigned long  reserved1             : 22;
    unsigned long  DetailFltClamp        :  6;
    unsigned long  FilterNoise           : 10;
    unsigned long  FilterDetail          : 10;
    unsigned long  ChromaNoise           : 10;
    unsigned long  ChromaDetail          : 10;
    unsigned long  DeinterlaceMode       :  4;
    unsigned long  MotionAccumWeight     :  3;
    unsigned long  NoiseIir              : 11;
    unsigned long  LightLevel            :  4;
    unsigned long  reserved4             :  2;
    unsigned int   SoftClampLow          : 10;
    unsigned int   SoftClampHigh         : 10;
    unsigned int   reserved5             :  3;
    unsigned int   reserved6             :  9;
    unsigned int   PlanarAlpha           : 10;
    unsigned int   ConstantAlpha         :  1;
    unsigned int   StereoInterleave      :  3;
    unsigned int   ClipEnabled           :  1;
    unsigned int   ClearRectMask         :  8;
    unsigned int   DegammaMode           :  2;
    unsigned int   reserved7             :  1;
    unsigned int   DecompressEnable      :  1;
    unsigned int   reserved9             :  5;
    unsigned long  DecompressCtbCount    :  8;
    unsigned long  DecompressZbcColor    : 32;
    unsigned long  reserved12            : 24;
    unsigned int   SourceRectLeft        : 30;
    unsigned int   reserved14            :  2;
    unsigned int   SourceRectRight       : 30;
    unsigned int   reserved15            :  2;
    unsigned int   SourceRectTop         : 30;
    unsigned int   reserved16            :  2;
    unsigned int   SourceRectBottom      : 30;
    unsigned int   reserved17            :  2;
    unsigned int   DestRectLeft          : 14;
    unsigned int   reserved18            :  2;
    unsigned int   DestRectRight         : 14;
    unsigned int   reserved19            :  2;
    unsigned int   DestRectTop           : 14;
    unsigned int   reserved20            :  2;
    unsigned int   DestRectBottom        : 14;
    unsigned int   reserved21            :  2;
    unsigned int   reserved22            : 32;
    unsigned int   reserved23            : 32;
} VicSlotStructSlotConfig;

typedef struct VicSlotStructSlotSurfaceConfig {
    unsigned int   SlotPixelFormat       :  7;
    unsigned int   SlotChromaLocHoriz    :  2;
    unsigned int   SlotChromaLocVert     :  2;
    unsigned int   SlotBlkKind           :  4;
    unsigned int   SlotBlkHeight         :  4;
    unsigned int   SlotCacheWidth        :  3;
    unsigned int   reserved0             : 10;
    unsigned int   SlotSurfaceWidth      : 14;
    unsigned int   SlotSurfaceHeight     : 14;
    unsigned int   reserved1             :  4;
    unsigned int   SlotLumaWidth         : 14;
    unsigned int   SlotLumaHeight        : 14;
    unsigned int   reserved2             :  4;
    unsigned int   SlotChromaWidth       : 14;
    unsigned int   SlotChromaHeight      : 14;
    unsigned int   reserved3             :  4;
} VicSlotStructSlotSurfaceConfig;

typedef struct VicSlotStructLumaKeyStruct {
    unsigned long  luma_coeff0           : 20;
    unsigned long  luma_coeff1           : 20;
    unsigned long  luma_coeff2           : 20;
    unsigned long  luma_r_shift          :  4;
    unsigned long  luma_coeff3           : 20;
    unsigned long  LumaKeyLower          : 10;
    unsigned long  LumaKeyUpper          : 10;
    unsigned long  LumaKeyEnabled        :  1;
    unsigned long  reserved0             :  2;
    unsigned long  reserved1             : 21;
} VicSlotStructLumaKeyStruct;

typedef struct VicSlotStructBlendingSlotStruct {
    unsigned int   AlphaK1               : 10;
    unsigned int   reserved0             :  6;
    unsigned int   AlphaK2               : 10;
    unsigned int   reserved1             :  6;
    unsigned int   SrcFactCMatchSelect   :  3;
    unsigned int   reserved2             :  1;
    unsigned int   DstFactCMatchSelect   :  3;
    unsigned int   reserved3             :  1;
    unsigned int   SrcFactAMatchSelect   :  3;
    unsigned int   reserved4             :  1;
    unsigned int   DstFactAMatchSelect   :  3;
    unsigned int   reserved5             :  1;
    unsigned int   reserved6             :  4;
    unsigned int   reserved7             :  4;
    unsigned int   reserved8             :  4;
    unsigned int   reserved9             :  4;
    unsigned int   reserved10            :  2;
    unsigned int   OverrideR             : 10;
    unsigned int   OverrideG             : 10;
    unsigned int   OverrideB             : 10;
    unsigned int   OverrideA             : 10;
    unsigned int   reserved11            :  2;
    unsigned int   UseOverrideR          :  1;
    unsigned int   UseOverrideG          :  1;
    unsigned int   UseOverrideB          :  1;
    unsigned int   UseOverrideA          :  1;
    unsigned int   MaskR                 :  1;
    unsigned int   MaskG                 :  1;
    unsigned int   MaskB                 :  1;
    unsigned int   MaskA                 :  1;
    unsigned int   reserved12            : 12;
} VicSlotStructBlendingSlotStruct;

typedef struct VicSlotStruct {
    VicSlotStructSlotConfig         slotConfig;
    VicSlotStructSlotSurfaceConfig  slotSurfaceConfig;
    VicSlotStructLumaKeyStruct      lumaKeyStruct;
    VicMatrixStruct                 colorMatrixStruct;
    VicMatrixStruct                 gamutMatrixStruct;
    VicSlotStructBlendingSlotStruct blendingSlotStruct;
} VicSlotStruct;

typedef struct VicConfigStruct {
    VicPipeConfig                   pipeConfig;
    VicOutputConfig                 outputConfig;
    VicOutputSurfaceConfig          outputSurfaceConfig;
    VicMatrixStruct                 outColorMatrixStruct;
    VicClearRectStruct              clearRectStruct[4];
    VicSlotStruct                   slotStruct[8];
} VicConfigStruct;

#endif // __VIC_DRV_H
