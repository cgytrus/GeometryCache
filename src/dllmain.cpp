#include "includes.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include "qoi/QOI.hpp"

const CCImage::EImageFormat kFmtQoi = (CCImage::EImageFormat)69;

void cacheImage(CCImage* self, std::filesystem::file_time_type qoiWriteTime, std::filesystem::path qoiPath) {
    auto m_nWidth = *(unsigned short*)((uintptr_t)self + 0x20);
    auto m_nHeight = *(unsigned short*)((uintptr_t)self + 0x22);
    auto m_pData = *(const uint8_t**)((uintptr_t)self + 0x28);
    auto m_bHasAlpha = *(bool*)((uintptr_t)self + 0x2c);
    auto m_bPreMulti = *(bool*)((uintptr_t)self + 0x2d);

    QOIEncoder encoder;

    if(!encoder.encode(m_nWidth, m_nHeight, m_pData, m_bHasAlpha, m_bPreMulti))
        return;

    std::ofstream qoiFile;
    qoiFile.open(qoiPath, std::ios::out | std::ios::trunc | std::ios::binary);
    qoiFile.write((const char*)encoder.getEncoded(), encoder.getEncodedSize());
    qoiFile.close();

    // used instead of actual hash for better performance
    std::filesystem::last_write_time(qoiPath, qoiWriteTime);
}

bool (__thiscall* CCImage_initWithImageFile)(CCImage*, const char*, CCImage::EImageFormat);
bool __fastcall CCImage_initWithImageFile_H(CCImage* self, void*, const char* strPath, CCImage::EImageFormat imageType) {
    auto qoiWriteTime = std::filesystem::last_write_time(strPath);
    auto qoiPath = std::filesystem::path(strPath).replace_extension("qoi");

    if(std::filesystem::exists(qoiPath) && std::filesystem::last_write_time(qoiPath) == qoiWriteTime)
        return CCImage_initWithImageFile(self, qoiPath.string().c_str(), kFmtQoi);

    auto success = CCImage_initWithImageFile(self, strPath, imageType);
    if(success)
        cacheImage(self, qoiWriteTime, qoiPath);
    return success;
}

bool (__thiscall* CCImage_initWithImageFileThreadSafe)(CCImage*, const char*, CCImage::EImageFormat);
bool __fastcall CCImage_initWithImageFileThreadSafe_H(CCImage* self, void*, const char* strPath, CCImage::EImageFormat imageType) {
    auto qoiWriteTime = std::filesystem::last_write_time(strPath);
    auto qoiPath = std::filesystem::path(strPath).replace_extension("qoi");

    if(std::filesystem::exists(qoiPath) && std::filesystem::last_write_time(qoiPath) == qoiWriteTime)
        return CCImage_initWithImageFileThreadSafe(self, qoiPath.string().c_str(), kFmtQoi);

    auto success = CCImage_initWithImageFileThreadSafe(self, strPath, imageType);
    if(success)
        cacheImage(self, qoiWriteTime, qoiPath);
    return success;
}

bool initWithQoiData(CCImage* self, void* pData, int nDataLen) {
    QOIDecoder decoder;

    if(!decoder.decode((const uint8_t*)pData, nDataLen))
        return false;

    *(unsigned short*)((uintptr_t)self + 0x20) = decoder.getWidth();
    *(unsigned short*)((uintptr_t)self + 0x22) = decoder.getHeight();
    *(int*)((uintptr_t)self + 0x24) = 8; // m_nBitsPerComponent
    *(unsigned char**)((uintptr_t)self + 0x28) = (unsigned char*)decoder.getPixels();
    *(bool*)((uintptr_t)self + 0x2c) = decoder.hasAlpha();
    // actually is premultiplied alpha but the linear colorspace field is not used so why not use it for this instead
    // this may make some (or all) images display weirdly in proper qoi viewers tho
    *(bool*)((uintptr_t)self + 0x2d) = decoder.isLinearColorspace();

    return true;
}

bool (__thiscall* CCImage_initWithImageData)(CCImage*, void*, int, CCImage::EImageFormat, int, int, int);
bool __fastcall CCImage_initWithImageData_H(CCImage* self, void*, void* pData, int nDataLen, CCImage::EImageFormat eFmt, int nWidth, int nHeight, int nBitsPerComponent) {
    return eFmt == kFmtQoi ?
        pData && nDataLen > 0 && initWithQoiData(self, pData, nDataLen) :
        CCImage_initWithImageData(self, pData, nDataLen, eFmt, nWidth, nHeight, nBitsPerComponent);
}

DWORD WINAPI mainThread(void* hModule) {
    MH_Initialize();

    auto base = reinterpret_cast<uintptr_t>(GetModuleHandle(0));
    auto cocos2dBase = reinterpret_cast<uintptr_t>(GetModuleHandle("libcocos2d.dll"));

    MH_CreateHook(reinterpret_cast<void*>(cocos2dBase + 0xc7380),
        reinterpret_cast<void*>(&CCImage_initWithImageFile_H),
        reinterpret_cast<void**>(&CCImage_initWithImageFile));

    MH_CreateHook(reinterpret_cast<void*>(cocos2dBase + 0xc7450),
        reinterpret_cast<void*>(&CCImage_initWithImageFileThreadSafe_H),
        reinterpret_cast<void**>(&CCImage_initWithImageFileThreadSafe));

    MH_CreateHook(reinterpret_cast<void*>(cocos2dBase + 0xc7280),
        reinterpret_cast<void*>(&CCImage_initWithImageData_H),
        reinterpret_cast<void**>(&CCImage_initWithImageData));

    MH_EnableHook(MH_ALL_HOOKS);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE handle, DWORD reason, LPVOID reserved) {
    if(reason == DLL_PROCESS_ATTACH)
        CreateThread(nullptr, 0x100, mainThread, handle, 0, nullptr);
    return TRUE;
}