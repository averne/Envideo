#ifndef _host1x_h_
#define _host1x_h_

#ifdef __cplusplus
extern "C" {
#endif

/* From L4T include/linux/host1x.h */
enum host1x_class {
    HOST1X_CLASS_HOST1X  = 0x01,
    HOST1X_CLASS_NVENC   = 0x21,
    HOST1X_CLASS_VI      = 0x30,
    HOST1X_CLASS_ISPA    = 0x32,
    HOST1X_CLASS_ISPB    = 0x34,
    HOST1X_CLASS_GR2D    = 0x51,
    HOST1X_CLASS_GR2D_SB = 0x52,
    HOST1X_CLASS_VIC     = 0x5d,
    HOST1X_CLASS_GR3D    = 0x60,
    HOST1X_CLASS_NVJPG   = 0xc0,
    HOST1X_CLASS_NVDEC   = 0xf0,
    HOST1X_CLASS_OFA     = 0xf8,
};

#define NVHOST_HCFSETCL_MASK                                               5:0
#define NVHOST_HCFSETCL_CLASSID                                            15:6
#define NVHOST_HCFSETCL_OFFSET                                             27:16
#define NVHOST_HCFSETCL_OPCODE                                             31:28
#define NVHOST_HCFSETCL_OPCODE_VALUE                                       (0x00000000)
#define NVHOST_HCFINCR_COUNT                                               15:0
#define NVHOST_HCFINCR_OFFSET                                              27:16
#define NVHOST_HCFINCR_OPCODE                                              31:28
#define NVHOST_HCFINCR_OPCODE_VALUE                                        (0x00000001)
#define NVHOST_HCFNONINCR_COUNT                                            15:0
#define NVHOST_HCFNONINCR_OFFSET                                           27:16
#define NVHOST_HCFNONINCR_OPCODE                                           31:28
#define NVHOST_HCFNONINCR_OPCODE_VALUE                                     (0x00000002)
#define NVHOST_HCFMASK_MASK                                                15:0
#define NVHOST_HCFMASK_OFFSET                                              27:16
#define NVHOST_HCFMASK_OPCODE                                              31:28
#define NVHOST_HCFMASK_OPCODE_VALUE                                        (0x00000003)
#define NVHOST_HCFIMM_IMMDATA                                              15:0
#define NVHOST_HCFIMM_OFFSET                                               27:16
#define NVHOST_HCFIMM_OPCODE                                               31:28
#define NVHOST_HCFIMM_OPCODE_VALUE                                         (0x00000004)

#define NV_CLASS_HOST_LOAD_SYNCPT_PAYLOAD                                  (0x00000138)
#define NV_CLASS_HOST_WAIT_SYNCPT                                          (0x00000140)

#define NV_THI_INCR_SYNCPT                                                 (0x00000000)
#define NV_THI_INCR_SYNCPT_INDX                                            7:0
#define NV_THI_INCR_SYNCPT_COND                                            15:8
#define NV_THI_INCR_SYNCPT_COND_IMMEDIATE                                  (0x00000000)
#define NV_THI_INCR_SYNCPT_COND_OP_DONE                                    (0x00000001)
#define NV_THI_INCR_SYNCPT_ERR                                             (0x00000008)
#define NV_THI_INCR_SYNCPT_ERR_COND_STS_IMM                                0:0
#define NV_THI_INCR_SYNCPT_ERR_COND_STS_OPDONE                             1:1
#define NV_THI_CTXSW_INCR_SYNCPT                                           (0x0000000c)
#define NV_THI_CTXSW_INCR_SYNCPT_INDX                                      7:0
#define NV_THI_CTXSW                                                       (0x00000020)
#define NV_THI_CTXSW_CURR_CLASS                                            9:0
#define NV_THI_CTXSW_AUTO_ACK                                              11:11
#define NV_THI_CTXSW_CURR_CHANNEL                                          15:12
#define NV_THI_CTXSW_NEXT_CLASS                                            25:16
#define NV_THI_CTXSW_NEXT_CHANNEL                                          31:28
#define NV_THI_CONT_SYNCPT_EOF                                             (0x00000028)
#define NV_THI_CONT_SYNCPT_EOF_INDEX                                       7:0
#define NV_THI_CONT_SYNCPT_EOF_COND                                        8:8
#define NV_THI_METHOD0                                                     (0x00000040)
#define NV_THI_METHOD0_OFFSET                                              11:0
#define NV_THI_METHOD1                                                     (0x00000044)
#define NV_THI_METHOD1_DATA                                                31:0
#define NV_THI_INT_STATUS                                                  (0x00000078)
#define NV_THI_INT_STATUS_FALCON_INT                                       0:0
#define NV_THI_INT_MASK                                                    (0x0000007c)
#define NV_THI_INT_MASK_FALCON_INT                                         0:0

#ifdef __cplusplus
};     /* extern "C" */
#endif

#endif /* _host1x_h_ */
