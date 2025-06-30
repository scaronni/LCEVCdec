/* Copyright (c) V-Nova International Limited 2023-2025. All rights reserved.
 * This software is licensed under the BSD-3-Clause-Clear License by V-Nova Limited.
 * No patent licenses are granted under this license. For enquiries about patent licenses,
 * please contact legal@v-nova.com.
 * The LCEVCdec software is a stand-alone project and is NOT A CONTRIBUTION to any other project.
 * If the software is incorporated into another project, THE TERMS OF THE BSD-3-CLAUSE-CLEAR LICENSE
 * AND THE ADDITIONAL LICENSING INFORMATION CONTAINED IN THIS FILE MUST BE MAINTAINED, AND THE
 * SOFTWARE DOES NOT AND MUST NOT ADOPT THE LICENSE OF THE INCORPORATING PROJECT. However, the
 * software may be incorporated into a project under a compatible license provided the requirements
 * of the BSD-3-Clause-Clear license are respected, and V-Nova Limited remains
 * licensor of the software ONLY UNDER the BSD-3-Clause-Clear license (not the compatible license).
 * ANY ONWARD DISTRIBUTION, WHETHER STAND-ALONE OR AS PART OF ANY OTHER PROJECT, REMAINS SUBJECT TO
 * THE EXCLUSION OF PATENT LICENSES PROVISION OF THE BSD-3-CLAUSE-CLEAR LICENSE. */

#include "utils.h"

#include <buffer_manager.h>
#include <core_interface.h>
#include <gtest/gtest.h>
#include <LCEVC/lcevc_dec.h>
#include <LCEVC/legacy/PerseusDecoder.h>
#include <LCEVC/pipeline/types.h>
#include <picture.h>
#include <picture_lock.h>

#include <algorithm>

// - Usings and consts ----------------------------------------------------------------------------

using namespace lcevc_dec::decoder;
using namespace lcevc_dec::pipeline;

static const uint32_t kBigRes[2] = {1920, 1080};
static const uint32_t kSmallRes[2] = {960, 540};
// Expected size for I420_8:
static const uint32_t kBigByteSize = kBigRes[0] * kBigRes[1] * 3 / 2;

static const LdpHDRStaticInfo kNonsenseHDRInfo = {
    4, 120, 34, 81, 104, 29, 9323, 1085, 245, 102, 62, 101,
};

static const uint32_t kBytesIn12Bits = 2;

static const uint8_t kYUVValues[kI420NumPlanes] = {'Y', 'U', 'V'};

// - Helper functions -----------------------------------------------------------------------------

static bool setDesc(PictureExternal& pic, const LdpPictureDesc& newDesc,
                    const LdpPicturePlaneDesc* planeDescArr, const LdpPictureBufferDesc& bufferDesc)
{
    return pic.setDescExternal(newDesc, planeDescArr, &bufferDesc);
}
static bool setDesc(PictureManaged& pic, const LdpPictureDesc& newDesc, const LdpPicturePlaneDesc*,
                    const LdpPictureBufferDesc&)
{
    return pic.setDesc(newDesc);
}
static bool setDesc(Picture& pic, const LdpPictureDesc& newDesc,
                    const LdpPicturePlaneDesc* planeDescArr, const LdpPictureBufferDesc& bufferDesc)
{
    if (pic.isManaged()) {
        return setDesc(static_cast<PictureManaged&>(pic), newDesc, planeDescArr, bufferDesc);
    }
    return setDesc(static_cast<PictureExternal&>(pic), newDesc, nullptr, bufferDesc);
}

static bool initPic(Picture& pic, SmartBuffer& bufferOut, LdpColorFormat format, uint32_t width,
                    uint32_t height, LdpAccelBuffer* accelBufferHandle, LdpAccess access)
{
    LdpPictureBufferDesc bufDesc = {};
    LdpPicturePlaneDesc planeDescArr[kLdpPictureMaxNumPlanes] = {};
    if (!pic.isManaged()) {
        setupPictureExternal(bufDesc, bufferOut, planeDescArr, format, width, height,
                             accelBufferHandle, access);
    }

    LdpPictureDesc desc = {};
    LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&desc, (LCEVC_ColorFormat)format, width, height);
    if (!setDesc(pic, desc, planeDescArr, bufDesc)) {
        return false;
    }

    return true;
}

// - Fixtures -------------------------------------------------------------------------------------

template <typename PicType>
class PictureFixture : public testing::Test
{
public:
    PictureFixture();

    void SetUp() override
    {
        setupPictureExternal(m_bufferDesc, m_externalBuffer, m_planeDescArr, LdpColorFormatI420_8,
                             kBigRes[0], kBigRes[1], nullptr, LdpAccessModify);
    }

    bool setDesc()
    {
        LdpPictureDesc defaultDesc;
        LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&defaultDesc, LCEVC_I420_8, kBigRes[0], kBigRes[1]);
        return ::setDesc(m_pic, defaultDesc, m_planeDescArr, m_bufferDesc);
    }

    PicType constructPic();

protected:
    // for managed pics:
    BufferManager m_BufMan;

    // for external pics:
    SmartBuffer m_externalBuffer;
    LdpPictureBufferDesc m_bufferDesc = {};
    LdpPicturePlaneDesc m_planeDescArr[kLdpPictureMaxNumPlanes] = {};

    PicType m_pic;
};

template <>
PictureFixture<PictureManaged>::PictureFixture()
    : m_pic(m_BufMan)
{}
template <>
PictureFixture<PictureExternal>::PictureFixture() = default;

template <>
inline PictureManaged PictureFixture<PictureManaged>::constructPic()
{
    return PictureManaged(m_BufMan);
}

template <>
inline PictureExternal PictureFixture<PictureExternal>::constructPic()
{
    return PictureExternal();
}

using PicTypes = ::testing::Types<PictureManaged, PictureExternal>;
TYPED_TEST_SUITE(PictureFixture, PicTypes);

using PicManFixture = PictureFixture<PictureManaged>;
using PicExtFixture = PictureFixture<PictureExternal>;

// - Tests ----------------------------------------------------------------------------------------

// - PictureExternal ----------------------------

TEST(pictureExternal, isManaged)
{
    PictureExternal pic;
    EXPECT_FALSE(pic.isManaged());
}

TEST_F(PicExtFixture, validSetDesc)
{
    // Succeed if desc is equal or smaller, even if it's a mismatched type (e.g. switching from
    // a I420 at high-res to an NV12 at low-res). Since we're switched to NV12, note that the byte
    // stride for the 2nd plane will be the same as that for the first.
    LdpPictureDesc desiredDesc = {};
    const LdpPictureBufferDesc& desiredBufferDesc = m_bufferDesc; // default is fine.
    LdpPicturePlaneDesc desiredPlaneDescs[3] = {
        {desiredBufferDesc.data, kSmallRes[0]},
        {desiredBufferDesc.data + kSmallRes[0] * kSmallRes[1], kSmallRes[0]},
        {desiredBufferDesc.data + kSmallRes[0] * kSmallRes[1], kSmallRes[0]}};
    ASSERT_EQ(LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&desiredDesc, LCEVC_NV12_8, kSmallRes[0],
                                       kSmallRes[1]),
              LCEVC_Success);
    EXPECT_TRUE(m_pic.setDescExternal(desiredDesc, desiredPlaneDescs, &desiredBufferDesc));

    LdpPictureDesc actualDesc = {};
    m_pic.getDesc(actualDesc);
    EXPECT_EQ(actualDesc, desiredDesc);

    LdpPictureBufferDesc actualBufDesc = {};
    EXPECT_TRUE(m_pic.getBufferDesc(actualBufDesc));
    EXPECT_EQ(actualBufDesc, desiredBufferDesc);

    for (uint32_t planeIdx = 0; planeIdx < m_pic.getNumPlanes(); planeIdx++) {
        EXPECT_EQ(m_pic.getPlaneFirstSample(planeIdx), desiredPlaneDescs[planeIdx].firstSample);
        EXPECT_EQ(m_pic.getPlaneByteStride(planeIdx), desiredPlaneDescs[planeIdx].rowByteStride);
    }
}

TEST_F(PicExtFixture, invalidSetDesc)
{
    LdpPictureDesc bigPictureDesc = {};
    ASSERT_EQ(LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&bigPictureDesc, LCEVC_I420_10_LE,
                                       kBigRes[0], kBigRes[1]),
              LCEVC_Success);
    SmartBuffer buffersDummy = {};

    // Fail if our buffer is small...
    LdpPictureBufferDesc newSmallBufferDesc = {};
    {
        LdpPicturePlaneDesc planeDescArrDummy[kLdpPictureMaxNumPlanes] = {};
        setupPictureExternal(newSmallBufferDesc, buffersDummy, planeDescArrDummy,
                             LdpColorFormatI420_8, kBigRes[0], kBigRes[1], nullptr, LdpAccessUnknown);
    }

    // ...But our planes are big (because they're 10bit)
    LdpPicturePlaneDesc newBigPlaneDescArr[kLdpPictureMaxNumPlanes] = {};
    {
        LdpPictureBufferDesc bufferDescDummy = {};
        setupPictureExternal(bufferDescDummy, buffersDummy, newBigPlaneDescArr, LdpColorFormatI420_10_LE,
                             kBigRes[0], kBigRes[1], nullptr, LdpAccessUnknown);
    }

    EXPECT_FALSE(m_pic.setDescExternal(bigPictureDesc, newBigPlaneDescArr, &newSmallBufferDesc));
}

TEST_F(PicExtFixture, getBuffer)
{
    setDesc();

    LdpPictureBufferDesc desc;
    EXPECT_TRUE(m_pic.getBufferDesc(desc));
    EXPECT_EQ(desc.accelBuffer, m_bufferDesc.accelBuffer);
    EXPECT_EQ(desc.access, m_bufferDesc.access);
    EXPECT_EQ(desc.byteSize, m_bufferDesc.byteSize);
    EXPECT_EQ(desc.data, m_bufferDesc.data);
}

// - PictureManaged -----------------------------

TEST(pictureManaged, isManaged)
{
    BufferManager arbitraryBufferManager;
    PictureManaged pic(arbitraryBufferManager);
    EXPECT_TRUE(pic.isManaged());
}

// Note: no invalidSetDesc here (though there are some in the general PictureFixture). This is
// because Managed Pictures can just bind extra memory if you give them a too-large PictureDesc.
TEST_F(PicManFixture, validSetDesc)
{
    LdpPictureDesc descToSet = {};
    LdpPictureDesc descToGet = {};

    ASSERT_EQ(LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&descToSet, LCEVC_NV12_8, kSmallRes[0],
                                       kSmallRes[1]),
              LCEVC_Success);
    EXPECT_TRUE(m_pic.setDesc(descToSet));
    m_pic.getDesc(descToGet);
    EXPECT_EQ(descToGet, descToSet);

    ASSERT_EQ(LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&descToSet, LCEVC_I420_10_LE, kBigRes[0],
                                       kBigRes[1]),
              LCEVC_Success);
    EXPECT_TRUE(m_pic.setDesc(descToSet));
    m_pic.getDesc(descToGet);
    EXPECT_EQ(descToGet, descToSet);
}

TEST_F(PicManFixture, getBuffer)
{
    setDesc();

    // Managed pictures currently store all planes in one buffer.
    EXPECT_EQ(m_pic.getNumPlanes(), kI420NumPlanes);
    LdpPictureBufferDesc desc;
    EXPECT_TRUE(m_pic.getBufferDesc(desc));
    EXPECT_EQ(desc.byteSize, kBigByteSize);
}

TEST(pictureManaged, bufferManagersDontOverlap)
{
    // Test that pictures with different buffer managers don't get buffers from each other's
    // manager. This can be done by creating two pictures, then releasing all buffers from one
    // buffer manager. The picture with the released buffer manager should have no buffer, while
    // the other should have a buffer as usual.
    BufferManager bufMan1;
    BufferManager bufMan2;
    PictureManaged pic1(bufMan1);
    PictureManaged pic2(bufMan2);

    LdpPictureDesc defaultDesc;
    LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&defaultDesc, LCEVC_I420_8, kBigRes[0], kBigRes[1]);
    pic1.setDesc(defaultDesc);
    pic2.setDesc(defaultDesc);

    bufMan1.release();

    LdpPictureDesc altDesc;
    LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&altDesc, LCEVC_NV12_8, kSmallRes[0], kSmallRes[1]);
    // Set desc should fail because it fails to unbind memory (because buffer is already gone).
    EXPECT_FALSE(pic1.setDesc(altDesc));
    // Set desc should succeed because it can unbind memory (because buffer still exists).
    EXPECT_TRUE(pic2.setDesc(altDesc));
}

// - Picture (i.e. managed and external) --------

TYPED_TEST(PictureFixture, setDescMatchesGet)
{
    // Provide a bunch of values that are definitely not the default values. Make sure width and
    // height are still even numbers though (for I420 validity)
    LdpPictureDesc crazyDesc = {};
    crazyDesc.colorFormat = LdpColorFormatI420_12_LE;
    crazyDesc.colorRange = LdpColorRangeLimited;
    crazyDesc.colorPrimaries = LdpColorPrimariesBT601_NTSC;
    crazyDesc.transferCharacteristics = LdpTransferCharacteristicsPQ;
    crazyDesc.cropBottom = 22;
    crazyDesc.cropLeft = 16;
    crazyDesc.cropRight = 98;
    crazyDesc.cropTop = 144;
    crazyDesc.hdrStaticInfo = kNonsenseHDRInfo;
    crazyDesc.height = 998 + crazyDesc.cropTop + crazyDesc.cropBottom;
    crazyDesc.sampleAspectRatioDen = 2;
    crazyDesc.sampleAspectRatioNum = 3;
    crazyDesc.width = 10 + crazyDesc.cropLeft + crazyDesc.cropRight;

    const uint32_t expectedHeight = crazyDesc.height - (crazyDesc.cropTop + crazyDesc.cropBottom);
    const uint32_t expectedWidth = crazyDesc.width - (crazyDesc.cropLeft + crazyDesc.cropRight);

    // Some miscellaneous extra setup is required for external pics, but is unused otherwise.
    SmartBuffer dummyBuf = {};
    setupPictureExternal(this->m_bufferDesc, dummyBuf, this->m_planeDescArr, crazyDesc.colorFormat,
                         crazyDesc.width, crazyDesc.height, nullptr, LdpAccessUnknown);
    EXPECT_TRUE(::setDesc(this->m_pic, crazyDesc, this->m_planeDescArr, this->m_bufferDesc));

    EXPECT_EQ(this->m_pic.getWidth(), expectedWidth);
    EXPECT_EQ(this->m_pic.getHeight(), expectedHeight);
    EXPECT_EQ(this->m_pic.getBitdepth(), 12); // LCEVC_I420_12_LE
    EXPECT_EQ(this->m_pic.getBytedepth(), kBytesIn12Bits);
    EXPECT_EQ(this->m_pic.getNumPlanes(), kI420NumPlanes);
    for (uint32_t planeIdx = 0; planeIdx < this->m_pic.getNumPlanes(); planeIdx++) {
        // I420 so chroma planes are half width and half height (rounded up).
        const uint32_t expectedSampleStride =
            (planeIdx == 0 ? crazyDesc.width : (crazyDesc.width + 1) / 2);
        const uint32_t expectedByteStride = kBytesIn12Bits * expectedSampleStride;
        const uint32_t expectedUncroppedPlaneHeight =
            (planeIdx == 0 ? crazyDesc.height : (crazyDesc.height + 1) / 2);
        const uint32_t expectedPlaneHeight = (planeIdx == 0 ? expectedHeight : (expectedHeight + 1) / 2);
        const uint32_t expectedPlaneWidth = (planeIdx == 0 ? expectedWidth : (expectedWidth + 1) / 2);
        EXPECT_EQ(this->m_pic.getPlaneHeight(planeIdx), expectedPlaneHeight);
        EXPECT_EQ(this->m_pic.getPlaneWidth(planeIdx), expectedPlaneWidth);
        EXPECT_EQ(this->m_pic.getPlaneBytesPerPixel(planeIdx),
                  kBytesIn12Bits); // would be double for nv12's chroma plane though.

        // Byte stride and width-in-bytes will be the same because we don't have any padding
        // (padding hasn't been implemented yet, see PictureExternal::setDescExternal).
        EXPECT_EQ(this->m_pic.getPlaneByteStride(planeIdx), expectedByteStride);
        EXPECT_EQ(this->m_pic.getPlaneWidthBytes(planeIdx), kBytesIn12Bits * expectedPlaneWidth);
        EXPECT_EQ(this->m_pic.getPlaneSampleStride(planeIdx), expectedSampleStride);

        EXPECT_EQ(this->m_pic.getPlaneMemorySize(planeIdx),
                  expectedByteStride * expectedUncroppedPlaneHeight);
    }
}

TYPED_TEST(PictureFixture, settersMatchGetters)
{
    this->m_pic.setTimestamp(123123);
    EXPECT_EQ(this->m_pic.getTimestamp(), 123123);
    this->m_pic.setUserData(this);
    EXPECT_EQ(this->m_pic.getUserData(), this);
    this->m_pic.setPublicFlag(LCEVC_PictureFlag_Interlaced, true);
    EXPECT_TRUE(this->m_pic.getPublicFlag(LCEVC_PictureFlag_Interlaced));
    EXPECT_FALSE(this->m_pic.getPublicFlag(LCEVC_PictureFlag_IDR));
}

TYPED_TEST(PictureFixture, invalidSetDesc)
{
    // Invalid crop
    LdpPictureDesc defaultDesc = {};
    ASSERT_EQ(LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&defaultDesc, LCEVC_I420_8, kBigRes[0],
                                       kBigRes[1]),
              LCEVC_Success);
    defaultDesc.cropBottom = kBigRes[1] * 2 / 3;
    defaultDesc.cropTop = kBigRes[1] * 2 / 3;
    EXPECT_FALSE(::setDesc(this->m_pic, defaultDesc, this->m_planeDescArr, this->m_bufferDesc));

    // Invalid enum
    LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&defaultDesc, LCEVC_I420_8, kBigRes[0], kBigRes[1]);
    defaultDesc.colorFormat = LdpColorFormatUnknown;
    EXPECT_FALSE(::setDesc(this->m_pic, defaultDesc, this->m_planeDescArr, this->m_bufferDesc));
}

TYPED_TEST(PictureFixture, copyData)
{
    // This is a little tough to test. What we want to do here is copy from one picture to another,
    // and check that the contents are the same. To simplify this, we set 3 magic numbers, which
    // are different for each plane, and unlikely to occur as junk memory.
    this->setDesc();

    // Init the source pic (make more challenging using NV12)
    TypeParam pic = this->constructPic();
    SmartBuffer nv12Buffer;
    initPic(pic, nv12Buffer, LdpColorFormatNV12_8, kBigRes[0], kBigRes[1], nullptr, LdpAccessModify);

    // Fill the picture with data. We do cheeky memory mangling here to manually interleave
    {
        memset(pic.getPlaneFirstSample(0), kYUVValues[0], pic.getPlaneMemorySize(0));

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        auto* ptrToPlane = reinterpret_cast<uint16_t*>(pic.getPlaneFirstSample(1));
        const uint16_t val = kYUVValues[2] << 8 | kYUVValues[1];
        const size_t count = pic.getPlaneMemorySize(1) / pic.getPlaneBytesPerPixel(1);
        std::fill_n(ptrToPlane, count, val);
    }

    // Now the actual copy, and check that it succeeded:
    EXPECT_TRUE(this->m_pic.copyData(pic));

    for (uint8_t plane = 0; plane < this->m_pic.getNumPlanes(); plane++) {
        const uint8_t nv12Plane = (plane < 1 ? plane : 1);
        const uint8_t* ptrToPlaneNV12 = pic.getPlaneFirstSample(nv12Plane);
        const uint8_t* ptrToPlaneI420 = this->m_pic.getPlaneFirstSample(plane);

        if (plane == 0) {
            EXPECT_EQ(memcmp(ptrToPlaneNV12, ptrToPlaneI420, this->m_pic.getPlaneMemorySize(plane) - 1), 0);
            continue;
        }

        // Can't really hack a memcmp thing so just manually iterate
        if (plane == 2) {
            ptrToPlaneNV12++;
        }

        for (uint32_t row = 0; row < pic.getPlaneHeight(nv12Plane); row++) {
            const uint8_t* ptrToRowNV12 = ptrToPlaneNV12 + row * pic.getPlaneByteStride(nv12Plane);
            const uint8_t* ptrToRowI420 = ptrToPlaneI420 + row * this->m_pic.getPlaneByteStride(plane);
            const uint8_t* const nv12End = ptrToRowNV12 + pic.getPlaneWidthBytes(nv12Plane);
            while (ptrToRowNV12 < nv12End) {
                // Assert here, so it fails fast, rather than printing one error per pixel.
                ASSERT_EQ(*ptrToRowNV12, *ptrToRowI420)
                    << "Failed at row " << row << " out of " << pic.getPlaneHeight(nv12Plane);
                ptrToRowNV12 += pic.getPlaneBytesPerPixel(nv12Plane);
                ptrToRowI420 += this->m_pic.getPlaneBytesPerPixel(plane);
            }
        }
    }
}

TYPED_TEST(PictureFixture, toCoreImage)
{
    this->setDesc();
    perseus_image coreImg = {};
    EXPECT_TRUE(this->m_pic.toCoreImage(coreImg));

    // bitdepth
    uint8_t bitdepth = 0;
    EXPECT_TRUE(fromCoreBitdepth(coreImg.depth, bitdepth));
    EXPECT_EQ(bitdepth, this->m_pic.getBitdepth());

    // interleaving
    LdpPictureDesc desc;
    this->m_pic.getDesc(desc);

    // stride and contents
    for (uint32_t planeIdx = 0; planeIdx < this->m_pic.getNumPlanes(); planeIdx++) {
        EXPECT_EQ(memcmp(this->m_pic.getPlaneFirstSample(planeIdx), coreImg.plane[planeIdx],
                         this->m_pic.getPlaneMemorySize(planeIdx)),
                  0);
        EXPECT_EQ(this->m_pic.getPlaneSampleStride(planeIdx), coreImg.stride[planeIdx]);
    }
}

TYPED_TEST(PictureFixture, lock)
{
    // Sanity check that it's modifiable before locking
    EXPECT_TRUE(this->setDesc());

    // Handle doesn't actually matter here (generation of handles is tested elsewhere, in a test
    // called pictureLockPoolInterface). Just has to be something valid, and 0 is valid.
    PictureLock* p = nullptr;
    this->m_pic.lock(LdpAccessRead, p);

    // Expect all modification to fail now, but it should still be possible to set it to the SAME
    // desc (trivial success)
    LdpPictureDesc newNV12Desc;
    LCEVC_DefaultPictureDesc((LCEVC_PictureDesc*)&newNV12Desc, LCEVC_NV12_8, 540, 960);
    EXPECT_FALSE(::setDesc(this->m_pic, newNV12Desc, this->m_planeDescArr, this->m_bufferDesc));
    if (!this->m_externalBuffer) {
        EXPECT_TRUE(this->setDesc());
    }

    this->m_pic.unlock(p);
    EXPECT_TRUE(::setDesc(this->m_pic, newNV12Desc, this->m_planeDescArr, this->m_bufferDesc));
}
