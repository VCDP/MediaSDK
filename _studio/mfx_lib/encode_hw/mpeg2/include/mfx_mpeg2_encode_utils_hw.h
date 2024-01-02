// Copyright (c) 2017 Intel Corporation
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef __MFX_MPEG2_ENCODE_HW_UTILS_H__
#define __MFX_MPEG2_ENCODE_HW_UTILS_H__

#include "mfx_common.h"


#if defined (MFX_ENABLE_MPEG2_VIDEO_ENCODE)

#include "mfxvideo++int.h"
#include "mfx_frames.h"
#include "mfx_mpeg2_enc_common_hw.h"
#include "umc_mpeg2_brc.h"
#include "mfx_brc_common.h"
#include "umc_video_brc.h"
#include "mfx_enc_common.h"

namespace MPEG2EncoderHW
{
    struct BRCFrameParams : mfxBRCFrameParam
    {
        mfxU16 picStruct;
    };

    class BrcIface
    {
    public:
        virtual ~BrcIface() {};
        virtual mfxStatus Init(mfxVideoParamEx_MPEG2  & video) = 0;
        virtual mfxStatus Reset(mfxVideoParamEx_MPEG2  & video) = 0;
        virtual void Close() = 0;
        virtual void GetQp(const BRCFrameParams& par, mfxBRCFrameCtrl &frameCtrl) = 0;
        virtual void GetQpForRecode(const BRCFrameParams& par, mfxBRCFrameCtrl &frameCtrl) = 0;
        virtual mfxF32 GetFractionalQp(const BRCFrameParams& par) = 0;
        virtual void SetQp(const BRCFrameParams& par, mfxBRCFrameCtrl &frameCtrl) = 0;
        virtual mfxU32 Report(const BRCFrameParams& par, mfxU32 userDataLength, mfxU32 maxFrameSize, mfxBRCFrameCtrl &frameCtrl) = 0;
        virtual mfxU32 GetMinFrameSize() = 0;
    };

    BrcIface * CreateBrc(mfxVideoParam const & video);

    class MPEG2SWBRC : public BrcIface
    {
    public:
        MPEG2SWBRC ():
            m_minSize(0),
            m_pBRC(0)
        {
            memset(&m_BRCLocal,0, sizeof(m_BRCLocal));
        }

        virtual ~MPEG2SWBRC()
        {
            Close();
        }

        void QuantIntoScaleTypeAndCode (int32_t quant_value, int32_t &q_scale_type, int32_t &quantiser_scale_code)
         {
             if(quant_value > 7 && quant_value <= 62) 
             {
                 q_scale_type = 0;
                 quantiser_scale_code = (quant_value + 1) >> 1;
             } 
             else 
             { // non-linear quantizer
                 q_scale_type = 1;
                 if(quant_value <= 8) 
                 {
                     quantiser_scale_code = quant_value;
                 }
                 else if (quant_value > 62) 
                 {
                     quantiser_scale_code = 25+((quant_value-64+4)>>3);
                 }
             }
             if(quantiser_scale_code < 1) 
             {
                 quantiser_scale_code = 1;
             }
             if(quantiser_scale_code > 31) 
             {
                 quantiser_scale_code = 31;
             } 
         }

        mfxStatus SetQuantDCPredAndDelay(mfxFrameParamMPEG2 *pFrameParams, mfxI32 quant_value, mfxU8 *pQuant)
        {
            int32_t        q_scale_type            = 0;
            int32_t        quantiser_scale_code    = 0;
            UMC::Status   ret                     = UMC::UMC_OK;
            QuantIntoScaleTypeAndCode (quant_value, q_scale_type, quantiser_scale_code);
            if (pFrameParams->FrameType &  MFX_FRAMETYPE_I)
            {
                if(quant_value < 15)
                    pFrameParams->IntraVLCformat = 1;
                else
                    pFrameParams->IntraVLCformat = 0;
            }
            pFrameParams->QuantScaleType = q_scale_type;
            pFrameParams->VBVDelay = 0xffff;
            if(quant_value  >= 8)
            {
                pFrameParams->IntraDCprecision = 0;
            }
            else if(quant_value  >= 4)
            {
                pFrameParams->IntraDCprecision = 1;
            }
            else
            {
                pFrameParams->IntraDCprecision = 2;
            }
            *pQuant = (mfxU8)quantiser_scale_code;
            return MFX_ERR_NONE;

        }

        mfxStatus   Init(mfxVideoParamEx_MPEG2 &video)
        {
            mfxStatus sts = MFX_ERR_NONE;
            mfxExtBRC * extBRC = video.pExtBRC;
            if (extBRC && extBRC->pthis)
            {
                m_pBRC = extBRC;
            }
            else
            {
                sts = HEVCExtBRC::Create(m_BRCLocal);
                m_pBRC = &m_BRCLocal;
            }
            sts = m_pBRC->Init(m_pBRC->pthis, &(video.mfxVideoParams));
            return sts;
        }

        void  Close()
        {
            if(m_pBRC->pthis)
            {
                m_pBRC->Close(m_pBRC->pthis);
            }
            HEVCExtBRC::Destroy(m_BRCLocal);
        }

        mfxStatus  Reset(mfxVideoParamEx_MPEG2 &video )
        {
            return m_pBRC->Reset(m_pBRC->pthis,&(video.mfxVideoParams));
        }

        mfxU32 Report(const BRCFrameParams& par, mfxU32 /*userDataLength*/, mfxU32 /*maxFrameSize*/, mfxBRCFrameCtrl &frameCtrl)
        {
            mfxBRCFrameStatus frame_sts = {};
            mfxBRCFrameParam frame_par = *((mfxBRCFrameParam*)&par);
            mfxStatus sts = m_pBRC->Update(m_pBRC->pthis,&frame_par, &frameCtrl, &frame_sts);
            MFX_CHECK(sts == MFX_ERR_NONE, (mfxU32)UMC::BRC_ERROR); // BRC_ERROR
            m_minSize = frame_sts.MinFrameSize;
            switch (frame_sts.BRCStatus)
            {
            case ::MFX_BRC_OK:
                return  UMC::BRC_OK;
            case ::MFX_BRC_BIG_FRAME:
                return UMC::BRC_ERR_BIG_FRAME;
            case ::MFX_BRC_SMALL_FRAME:
                return UMC::BRC_ERR_SMALL_FRAME;
            case ::MFX_BRC_PANIC_BIG_FRAME:
                return UMC::BRC_ERR_BIG_FRAME |  UMC::BRC_NOT_ENOUGH_BUFFER;
            case ::MFX_BRC_PANIC_SMALL_FRAME:
                return UMC::BRC_ERR_SMALL_FRAME| UMC::BRC_NOT_ENOUGH_BUFFER;
            }
            return MFX_BRC_OK;
        }

        void GetQp(const BRCFrameParams& par, mfxBRCFrameCtrl &frameCtrl)
        {
            mfxBRCFrameParam frame_par = *((mfxBRCFrameParam*)&par);
            m_pBRC->GetFrameCtrl(m_pBRC->pthis,&frame_par, &frameCtrl);
            frameCtrl.QpY = (mfxU8)mfx::clamp(frameCtrl.QpY, 1, 112);
        }

        void GetQpForRecode(const BRCFrameParams& par, mfxBRCFrameCtrl &frameCtrl)
        {
            GetQp(par, frameCtrl);
        }

        void   SetQp(const BRCFrameParams& /*par*/, mfxBRCFrameCtrl &/*frameCtrl*/)
        {
        }

        mfxU32 GetMinFrameSize()
        {
            return m_minSize;
        }

        mfxF32 GetFractionalQp(const BRCFrameParams& /*par*/) { assert(0); return 0.0f; }
    
    private:
        mfxU32      m_minSize;
        mfxExtBRC * m_pBRC;
        mfxExtBRC   m_BRCLocal;
    };

    enum HW_MODE
    {
        FULL_ENCODE,
        HYBRID_ENCODE,
        UNSUPPORTED   
    };
    class EncoderBase
    {
    public:
        // Destructor
        virtual
            ~EncoderBase(void) {}

        virtual
            mfxStatus Init(mfxVideoParam *par) = 0;
        virtual
            mfxStatus Reset(mfxVideoParam *par) = 0;
        virtual
            mfxStatus Close(void) = 0;

        virtual
            mfxStatus GetVideoParam(mfxVideoParam *par) = 0;
        virtual
            mfxStatus GetEncodeStat(mfxEncodeStat *stat) = 0;
        virtual
            mfxStatus EncodeFrameCheck(mfxEncodeCtrl *ctrl,
            mfxFrameSurface1 *surface,
            mfxBitstream *bs,
            mfxFrameSurface1 **reordered_surface,
            mfxEncodeInternalParams *pInternalParams,
            MFX_ENTRY_POINT *pEntryPoint) = 0;
        virtual
            mfxStatus EncodeFrameCheck(mfxEncodeCtrl *ctrl,
            mfxFrameSurface1 *surface,
            mfxBitstream *bs,
            mfxFrameSurface1 **reordered_surface,
            mfxEncodeInternalParams *pInternalParams,
            MFX_ENTRY_POINT pEntryPoints[],
            mfxU32 &numEntryPoints)
        {
            mfxStatus mfxRes;

            // call the overweighted version
            mfxRes = EncodeFrameCheck(ctrl, surface, bs, reordered_surface, pInternalParams, pEntryPoints);
            numEntryPoints = 1;

            return mfxRes;
        }
        virtual
            mfxStatus EncodeFrameCheck(mfxEncodeCtrl *ctrl, mfxFrameSurface1 *surface, mfxBitstream *bs, mfxFrameSurface1 **reordered_surface, mfxEncodeInternalParams *pInternalParams) = 0;
        virtual
            mfxStatus EncodeFrame(mfxEncodeCtrl *ctrl, mfxEncodeInternalParams *pInternalParams, mfxFrameSurface1 *surface, mfxBitstream *bs) = 0;
        virtual
            mfxStatus CancelFrame(mfxEncodeCtrl *ctrl, mfxEncodeInternalParams *pInternalParams, mfxFrameSurface1 *surface, mfxBitstream *bs) = 0;
        virtual
            mfxTaskThreadingPolicy GetThreadingPolicy(void) = 0;

    };
    class FramesSet
    {
    public:
        mfxFrameSurface1    *m_pInputFrame;
        mfxFrameSurface1    *m_pRefFrame[2];
        mfxFrameSurface1    *m_pRawFrame[2];
        mfxFrameSurface1    *m_pRecFrame;
        mfxU32               m_nFrame;
        mfxU32               m_nRefFrame[2];
        mfxI32               m_nLastRefBeforeIntra;
        mfxI32               m_nLastRef;
    public:        
        FramesSet();
        void Reset();
        mfxStatus ReleaseFrames(VideoCORE* pCore);
        mfxStatus LockRefFrames(VideoCORE* pCore);
        inline mfxStatus LockFrames(VideoCORE* pCore)
        {
            if (m_pInputFrame)
            {
                MFX_CHECK_STS (pCore->IncreaseReference(&m_pInputFrame->Data));
            }
            if (m_pRecFrame)
            {
                MFX_CHECK_STS (pCore->IncreaseReference(&m_pRecFrame->Data));
            }        
            return LockRefFrames(pCore);
        }
    };
    mfxStatus CheckHwCaps(  VideoCORE* core, 
                            mfxVideoParam const * par, 
                            mfxExtCodingOption const * ext = 0, 
                            ENCODE_CAPS*  pCaps = 0);

    HW_MODE GetHwEncodeMode(ENCODE_CAPS &caps);

    mfxStatus ApplyTargetUsage(mfxVideoParamEx_MPEG2* par);

    mfxExtCodingOptionSPSPPS* GetExtCodingOptionsSPSPPS(mfxExtBuffer** ebuffers,
                                                        mfxU32 nbuffers);
    mfxStatus CheckExtendedBuffers (mfxVideoParam* par);

    mfxStatus UnlockFrames   (MFXGOP* pGOP, MFXWaitingList* pWaitingList, VideoCORE* pcore);

    mfxStatus FillMFXFrameParams(mfxFrameParamMPEG2*     pFrameParams,
                              mfxU8 frameType,
                              mfxVideoParamEx_MPEG2 *pExParams,
                              mfxU16 surface_pict_struct,
                              bool bBackwOnly,
                              bool bFwdOnly);


    class ControllerBase
    {
    private:
        VideoCORE*                      m_pCore;
        mfxU32                          m_nEncodeCalls;
        mfxU32                          m_nFrameInGOP;

        MFXGOP*                         m_pGOP;
        MFXWaitingList*                 m_pWaitingList;
        mfxI32                          m_InputFrameOrder;
        mfxI32                          m_OutputFrameOrder;
        mfxI32                          m_ExeFrameOrder;
        mfxU64                          m_BitstreamLen;    
        mfxVideoParamEx_MPEG2           m_VideoParamsEx;

        InputSurfaces                   m_InputSurfaces;

        mfxU16                          m_InitWidth;
        mfxU16                          m_InitHeight;
        bool                            m_bInitialized;
        bool                            m_bAVBR_WA;

    protected:
        inline bool is_initialized ()
        {return m_bInitialized;}

    public:

        ControllerBase(VideoCORE *core, bool bAVBR_WA = false);
        ~ControllerBase()
        {
            Close();
        }
        mfxStatus Close(void);
        mfxStatus Reset(mfxVideoParam *par, bool bAllowRawFrames);
        mfxStatus GetVideoParam(mfxVideoParam *par);
        mfxStatus GetFrameParam(mfxFrameParam *par);
        mfxStatus GetEncodeStat(mfxEncodeStat *stat);
        mfxStatus EncodeFrameCheck(mfxEncodeCtrl *ctrl, 
            mfxFrameSurface1 *surface, 
            mfxBitstream *bs, 
            mfxFrameSurface1 **reordered_surface, 
            mfxEncodeInternalParams *pInternalParams);

        static mfxStatus  
            Query(VideoCORE *core, mfxVideoParam *in, mfxVideoParam *out, bool bAVBR_WA = false);
        static mfxStatus  
            QueryIOSurf  (VideoCORE *core, mfxVideoParam *par, mfxFrameAllocRequest *request);

        mfxStatus ReorderFrame(mfxEncodeInternalParams *pInInternalParams, mfxFrameSurface1 *in,
            mfxEncodeInternalParams *pOutInternalParams, mfxFrameSurface1 **out);

        mfxStatus CheckNextFrame(mfxEncodeInternalParams *pOutInternalParams, mfxFrameSurface1 **out);

        mfxStatus CheckFrameType   (mfxEncodeInternalParams *pInternalParams); 

        inline bool isHWInput () {return !m_InputSurfaces.isSysMemFrames();}
        inline bool isOpaq() { return m_InputSurfaces.isOpaq(); }

        inline mfxU16 GetInputFrameType() 
        {
            return (mfxU16)( MFX_MEMTYPE_EXTERNAL_FRAME |
                            (m_InputSurfaces.isSysMemFrames() ? MFX_MEMTYPE_SYSTEM_MEMORY : MFX_MEMTYPE_DXVA2_DECODER_TARGET)|
                             MFX_MEMTYPE_FROM_ENCODE);
        }

        inline bool isRawFrames() 
        {return m_VideoParamsEx.bRawFrames;}

        inline mfxVideoParamEx_MPEG2* getVideoParamsEx()
        {return &m_VideoParamsEx;}

        inline mfxI32 GetFrameNumber()
        { return m_OutputFrameOrder; }

        inline void FinishFrame(mfxU32 frameSize)
        {
            m_OutputFrameOrder ++;
            m_BitstreamLen = m_BitstreamLen + frameSize;
        }
        inline void ExeFrameAdd()
        {
            m_ExeFrameOrder++;
        }
        mfxI32 GetExeFrameOrder ()
        {
            return m_ExeFrameOrder;
        }
        mfxI32 GetOutputFrameOrder ()
        {
            return m_OutputFrameOrder;
        }

        inline void SetAllocResponse(mfxFrameAllocResponse* pAR, bool bHW)
        {
            if (bHW) 
                m_VideoParamsEx.pRecFramesResponse_hw = pAR;
            else
                m_VideoParamsEx.pRecFramesResponse_sw = pAR;
        }

        mfxFrameSurface1 *GetOriginalSurface(mfxFrameSurface1 *surface)
        {
            return m_InputSurfaces.GetOriginalSurface(surface);
        }

        mfxFrameSurface1 *GetOpaqSurface(mfxFrameSurface1 *surface)
        {
            return m_InputSurfaces.GetOpaqSurface(surface);
        } 


    }; // class ControllerBase


    class MPEG2BRC_HW
    {
    private:
        UMC::VideoBrc*   m_pBRC;
        mfxU32           m_bConstantQuant;
        mfxU32           m_MinFrameSizeBits[3];
        mfxU32           m_MinFieldSizeBits[3];
        VideoCORE*       m_pCore;
        mfxU32           m_FirstGopSize;
        mfxU32           m_GopSize;
        mfxU32           m_bufferSizeInKB; 
        mfxI32           m_InputBitsPerFrame;
        bool             m_bLimitedMode;

    protected:

        void QuantIntoScaleTypeAndCode (int32_t quant_value, int32_t &q_scale_type, int32_t &quantiser_scale_code);
        inline int32_t ScaleTypeAndCodeIntoQuant (int32_t q_scale_type, int32_t quantiser_scale_code)
        {
            static int32_t Val_QScale[2][32] =
            {
                /* linear q_scale */
                {0,  2,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
                32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62},
                /* non-linear q_scale */
                {0, 1,  2,  3,  4,  5,  6,  7,  8, 10, 12, 14, 16, 18, 20, 22,
                24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96,104,112}};

                return Val_QScale[q_scale_type][quantiser_scale_code];
        }


        int32_t ChangeQuant(int32_t quant_value_old, int32_t quant_value_new);

    public:
        MPEG2BRC_HW(VideoCORE* pCore)
        {
            m_pCore          = pCore;
            m_pBRC           = 0;
            m_bConstantQuant = 0;
            m_MinFrameSizeBits [0] = 0;
            m_MinFrameSizeBits [1] = 0;
            m_MinFrameSizeBits [2] = 0;

            m_MinFieldSizeBits [0] = 0;
            m_MinFieldSizeBits [1] = 0;
            m_MinFieldSizeBits [2] = 0;

            m_FirstGopSize = 0;
            m_GopSize = 0;
            m_bufferSizeInKB = 0;
            m_InputBitsPerFrame = 0;
            m_bLimitedMode = 0;

        }
        ~MPEG2BRC_HW ()
        {
            Close();
        }
        mfxStatus Init(mfxVideoParam* par);

        inline mfxStatus Reset(mfxVideoParam* par)
        {
            return Init(par);
        }
        void Close ();

        mfxStatus StartNewFrame(const mfxFrameParamMPEG2 *pFrameParams, mfxI32 recode);
        mfxStatus SetQuantDCPredAndDelay(mfxFrameParamMPEG2 *pFrameParams, mfxU8 *pQuant);

        mfxStatus UpdateBRC(const mfxFrameParamMPEG2 *pParams, mfxBitstream* pBitsream, mfxU32 bitsize, mfxU32 numEncodedFrame, bool bNotEnoughBuffer ,mfxI32 &recode);
        inline void SetQuant(mfxU32 Quant, mfxU32 FrameType)
        {
            if (m_pBRC && m_bConstantQuant)
            {
                if (Quant > 0)
                {
                    if  (FrameType & MFX_FRAMETYPE_I)
                        m_pBRC->SetQP(Quant, UMC::I_PICTURE);
                    else if  (FrameType & MFX_FRAMETYPE_P)
                        m_pBRC->SetQP(Quant, UMC::P_PICTURE);
                    else if  (FrameType & MFX_FRAMETYPE_B)
                        m_pBRC->SetQP(Quant, UMC::B_PICTURE);
                }
            }    
        }

        inline bool   IsSkipped (mfxI32 &recode)
        {
            if (m_bLimitedMode && recode == 0)
            {
                recode = UMC::BRC_EXT_FRAMESKIP;
                return true;        
            }
            else if (m_bLimitedMode)
            {
                return true;
            }
            return false;
        }
    private:
        // Declare private copy constructor to avoid accidental assignment
        // and klocwork complaining.
        MPEG2BRC_HW(const MPEG2BRC_HW &);
        MPEG2BRC_HW & operator = (const MPEG2BRC_HW &);
    };

    enum TaskStatus
    {
        NOT_STARTED = 0,
        INPUT_READY = 1,
        ENC_STARTED = 2,
        ENC_READY   = 3,
    };

    class FrameStore
    {
    private:
        FrameStore(const FrameStore&);      // non-copyable
        void operator=(const FrameStore&);  // non-copyable

        mfxU16                  m_InputType;
        bool                    m_bHWFrames;

        mfxFrameSurface1        *m_pRefFrame[2];
        mfxFrameSurface1        *m_pRawFrame[2];
        mfxU32                  m_nRefFrame[2];

        mfxFrameSurface1        *m_pRefFramesStore;   // reference frames
        mfxFrameSurface1        *m_pInputFramesStore; // input frames in case of system memory
        mfxU32                  m_nRefFrames;
        mfxU32                  m_nInputFrames;
        VideoCORE*              m_pCore;
        mfxFrameAllocRequest    m_RefRequest;
        mfxFrameAllocResponse   m_RefResponse;
        mfxFrameAllocRequest    m_InputRequest;
        mfxFrameAllocResponse   m_InputResponse;
        mfxU32                  m_nFrame;
        mfxI32                  m_nLastRefBeforeIntra;
        mfxI32                  m_nLastRef;
        bool                    m_bRawFrame;

    protected:
        mfxStatus ReleaseFrames();
        mfxStatus GetInternalRefFrame(mfxFrameSurface1** ppFrame);
        mfxStatus GetInternalInputFrame(mfxFrameSurface1** ppFrame);

    public:

        FrameStore(VideoCORE* pCore)
        {
            m_InputType    = 0;
            m_bHWFrames    = false;

            memset (m_pRefFrame,0,sizeof(mfxFrameSurface1*)*2);
            memset (m_pRawFrame,0,sizeof(mfxFrameSurface1*)*2);
            memset (m_nRefFrame,0,sizeof(mfxU32)*2);

            m_pRefFramesStore = 0;
            m_pInputFramesStore = 0;
            m_nRefFrames = 0;
            m_nInputFrames = 0;

            m_nFrame = 0;
            m_nRefFrame[0] = 0;
            m_nRefFrame[1] = 0;
            m_nLastRefBeforeIntra = -1;
            m_nLastRef = -1;

            memset(&m_RefRequest, 0,  sizeof(mfxFrameAllocRequest));
            memset(&m_RefResponse, 0, sizeof(mfxFrameAllocResponse));
            memset(&m_InputRequest, 0,  sizeof(mfxFrameAllocRequest));
            memset(&m_InputResponse, 0, sizeof(mfxFrameAllocResponse));

            m_bRawFrame = 0;
            m_pCore = pCore; 
        }

        ~FrameStore()
        {
            Close();
        }

        inline mfxStatus Reset(bool bRawFrame, mfxU16 InputFrameType, bool bHWFrames, mfxU32 nTasks, mfxFrameInfo* pFrameInfo, bool bProtected = false)
        {
            return Init(bRawFrame, InputFrameType, bHWFrames, nTasks, pFrameInfo, bProtected);
        }

        mfxStatus Init(bool bRawFrame, mfxU16 InputFrameType, bool bHWFrames, mfxU32 mTasks, mfxFrameInfo* pFrameInfo, bool bProtected = false);

        mfxStatus NextFrame(mfxFrameSurface1 *pInputFrame, mfxU32 nFrame, mfxU16 frameType, mfxU32 intFlags, FramesSet *pFrames);

        mfxStatus Close();

        inline mfxFrameAllocResponse* GetFrameAllocResponse()
        {
            return  &m_RefResponse;
        }
    };

    class EncodeFrameTask
    {
    public:
        TaskStatus          m_taskStatus;
        mfxFrameParamMPEG2  m_FrameParams;
        mfxBitstream*       m_pBitstream;
        mfxU32              m_nEncodedOrder;
        FramesSet           m_Frames;
        VideoCORE*          m_pCore;
        mfxU32              m_nFrameOrder;
        mfxEncodeInternalParams 
                            m_sEncodeInternalParams;
        mfxU32              m_FeedbackNumber;
        mfxU32              m_BitstreamFrameNumber;
        BRCFrameParams      m_brcFrameParams;
        mfxBRCFrameCtrl     m_brcFrameCtrl;
    protected:
        inline
        void Reset()
        {
            memset(&m_FrameParams,0,sizeof(mfxFrameParamMPEG2));
            memset(&m_sEncodeInternalParams, 0, sizeof(mfxEncodeInternalParams));
            m_pBitstream = 0;
            m_pCore = 0;
            m_nFrameOrder = 0;
            m_taskStatus = NOT_STARTED;
            m_Frames.Reset();
            m_FeedbackNumber = 0;
            m_BitstreamFrameNumber = 0;
            m_nEncodedOrder=0;
            memset(&m_brcFrameParams,0,sizeof(BRCFrameParams));
            memset(&m_brcFrameCtrl,0,sizeof(mfxBRCFrameCtrl));
        }

    public:
        EncodeFrameTask()
        {
            Reset();
        }
        inline void InitBRCParams()
        {
            memset(&m_brcFrameParams, 0, sizeof(BRCFrameParams));
            memset(&m_brcFrameCtrl, 0, sizeof(mfxBRCFrameCtrl));
            m_brcFrameParams.FrameType = m_FrameParams.FrameType;
            m_brcFrameParams.DisplayOrder = m_nFrameOrder;
            m_brcFrameParams.EncodedOrder = m_nEncodedOrder;
        }
        inline
        mfxStatus Reset(VideoCORE* pCore)
        {
            mfxStatus sts = MFX_ERR_NONE;
            sts = m_Frames.ReleaseFrames(m_pCore);
            Reset();
            m_pCore = pCore;
            return sts;
        }
        inline
        mfxStatus Close()
        {
            mfxStatus sts = MFX_ERR_NONE;
            sts = m_Frames.ReleaseFrames(m_pCore);
            Reset();
            return sts;
        }

        inline 
        void SetFrames(FramesSet *pFrames,
                       mfxEncodeInternalParams* pEncodeInternalParams)
        {
            m_Frames = *pFrames;
            m_sEncodeInternalParams = *pEncodeInternalParams;
        }
        inline
            mfxStatus FillFrameParams (mfxU8 frameType, mfxVideoParamEx_MPEG2 *pExParams, mfxU16 surface_pict_struct, bool bBackwOnly, bool bFwdOnly, bool bAddSH, bool bAddEOS)
        {
            mfxStatus sts = MFX_ERR_NONE;
            sts = FillMFXFrameParams(&m_FrameParams, frameType, pExParams, surface_pict_struct, bBackwOnly, bFwdOnly);
            MFX_CHECK_STS(sts);
            m_FrameParams.TemporalReference = (mfxU16)(m_Frames.m_nFrame - m_Frames.m_nLastRefBeforeIntra - 1);
            m_FrameParams.ExtraFlags = (mfxU16)(( bAddSH ? MFX_IFLAG_ADD_HEADER:0)| (bAddEOS ? MFX_IFLAG_ADD_EOS:0)); // those values are not declared
            return sts;
        }
    };
  
}


#endif // MFX_ENABLE_MPEG2_VIDEO_ENCODE
#endif
