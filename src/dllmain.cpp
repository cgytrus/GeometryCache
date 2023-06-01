#include "qoixx.hpp"

#include "includes.h"
#include <filesystem>
#include <iostream>
#include <fstream>

const CCImage::EImageFormat kFmtQoi = (CCImage::EImageFormat)69;

void cacheImage(CCImage* self, std::filesystem::file_time_type qoiWriteTime, std::filesystem::path qoiPath) {
    auto m_nWidth = *(unsigned short*)((uintptr_t)self + 0x20);
    auto m_nHeight = *(unsigned short*)((uintptr_t)self + 0x22);
    auto m_pData = *(unsigned char**)((uintptr_t)self + 0x28);
    auto m_bHasAlpha = *(bool*)((uintptr_t)self + 0x2c);
    auto m_bPreMulti = *(bool*)((uintptr_t)self + 0x2d);

    qoixx::qoi::desc desc {
        .width = m_nWidth,
        .height = m_nHeight,
        .channels = m_bHasAlpha ? (uint8_t)4u : (uint8_t)3u,
        .colorspace = m_bPreMulti ? qoixx::qoi::colorspace::linear : qoixx::qoi::colorspace::srgb
    };

    try {
        auto encoded = qoixx::qoi::encode<std::vector<char>>(m_pData, desc.width * desc.height * desc.channels, desc);

        std::ofstream qoiFile;
        qoiFile.open(qoiPath, std::ios::out | std::ios::trunc | std::ios::binary);
        qoiFile.write(encoded.data(), encoded.size());
        qoiFile.close();

        // used instead of actual hash for better performance
        std::filesystem::last_write_time(qoiPath, qoiWriteTime);
    }
    catch(...) { }
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
    try {
        auto decoded = qoixx::qoi::decode<std::vector<unsigned char>>((unsigned char*)pData, nDataLen);

        *(unsigned short*)((uintptr_t)self + 0x20) = decoded.second.width;
        *(unsigned short*)((uintptr_t)self + 0x22) = decoded.second.height;
        *(int*)((uintptr_t)self + 0x24) = 8; // m_nBitsPerComponent
        *(bool*)((uintptr_t)self + 0x2c) = decoded.second.channels >= 4u;
        // actually is premultiplied alpha but the linear colorspace field is not used so why not use it for this instead
        // this may make some (or all) images display weirdly in proper qoi viewers tho
        *(bool*)((uintptr_t)self + 0x2d) = decoded.second.colorspace == qoixx::qoi::colorspace::linear;

        // copy decoded image to CCImage
        auto m_pData = (unsigned char**)((uintptr_t)self + 0x28);
        *m_pData = new unsigned char[decoded.first.size()];
        for(size_t i = 0; i < decoded.first.size(); ++i)
            (*m_pData)[i] = decoded.first[i];

        return true;
    }
    catch(...) {
        return false;
    }
}

bool (__thiscall* CCImage_initWithImageData)(CCImage*, void*, int, CCImage::EImageFormat, int, int, int);
bool __fastcall CCImage_initWithImageData_H(CCImage* self, void*, void* pData, int nDataLen, CCImage::EImageFormat eFmt, int nWidth, int nHeight, int nBitsPerComponent) {
    return eFmt == kFmtQoi ?
        pData && nDataLen > 0 && initWithQoiData(self, pData, nDataLen) :
        CCImage_initWithImageData(self, pData, nDataLen, eFmt, nWidth, nHeight, nBitsPerComponent);
}

// force RGBA8888
void (__cdecl* CCTexture2D_setDefaultAlphaPixelFormat)(CCTexture2DPixelFormat);
void __cdecl CCTexture2D_setDefaultAlphaPixelFormat_H(CCTexture2DPixelFormat format) {
    CCTexture2D_setDefaultAlphaPixelFormat(kCCTexture2DPixelFormat_RGBA8888);
}

void hook(HMODULE module, const char* symbol, void* detour, void** orig) {
    MH_CreateHook(reinterpret_cast<void*>(GetProcAddress(module, symbol)), detour, orig);
}

DWORD WINAPI mainThread(void* hModule) {
    MH_Initialize();

    auto cocos2d = GetModuleHandle("libcocos2d.dll");

    hook(cocos2d, "?initWithImageFile@CCImage@cocos2d@@QAE_NPBDW4EImageFormat@12@@Z",
        reinterpret_cast<void*>(&CCImage_initWithImageFile_H),
        reinterpret_cast<void**>(&CCImage_initWithImageFile));

    hook(cocos2d, "?initWithImageFileThreadSafe@CCImage@cocos2d@@QAE_NPBDW4EImageFormat@12@@Z",
        reinterpret_cast<void*>(&CCImage_initWithImageFileThreadSafe_H),
        reinterpret_cast<void**>(&CCImage_initWithImageFileThreadSafe));

    hook(cocos2d, "?initWithImageData@CCImage@cocos2d@@QAE_NPAXHW4EImageFormat@12@HHH@Z",
        reinterpret_cast<void*>(&CCImage_initWithImageData_H),
        reinterpret_cast<void**>(&CCImage_initWithImageData));

    hook(cocos2d, "?setDefaultAlphaPixelFormat@CCTexture2D@cocos2d@@SAXW4CCTexture2DPixelFormat@2@@Z",
        reinterpret_cast<void*>(&CCTexture2D_setDefaultAlphaPixelFormat_H),
        reinterpret_cast<void**>(&CCTexture2D_setDefaultAlphaPixelFormat));

    MH_EnableHook(MH_ALL_HOOKS);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE handle, DWORD reason, LPVOID reserved) {
    if(reason == DLL_PROCESS_ATTACH)
        CreateThread(nullptr, 0x100, mainThread, handle, 0, nullptr);
    return TRUE;
}