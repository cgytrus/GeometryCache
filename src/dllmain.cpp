#include "includes.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include "qoi/QOI.hpp"

bool _isQoi = false;
std::filesystem::path _qoiPath;
std::filesystem::file_time_type _qoiWriteTime;
bool (__thiscall* CCImage_initWithImageFile)(CCImage*, const char*, CCImage::EImageFormat);
bool __fastcall CCImage_initWithImageFile_H(CCImage* self, void*, const char* strPath, CCImage::EImageFormat imageType) {
    _qoiWriteTime = std::filesystem::last_write_time(strPath);
    _qoiPath = std::filesystem::path(strPath).replace_extension("qoi");

    if(std::filesystem::exists(_qoiPath) && std::filesystem::last_write_time(_qoiPath) == _qoiWriteTime) {
        _isQoi = true;
        return CCImage_initWithImageFile(self, _qoiPath.string().c_str(), imageType);
    }

    _isQoi = false;
    return CCImage_initWithImageFile(self, strPath, imageType);
}

bool (__thiscall* CCImage__initWithPngData)(CCImage*, void*, int);
bool __fastcall CCImage__initWithPngData_H(CCImage* self, void*, void* pData, int nDataLen) {
    if(_isQoi) {
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

    if(!CCImage__initWithPngData(self, pData, nDataLen))
        return false;

    auto m_nWidth = *(unsigned short*)((uintptr_t)self + 0x20);
    auto m_nHeight = *(unsigned short*)((uintptr_t)self + 0x22);
    auto m_pData = *(const uint8_t**)((uintptr_t)self + 0x28);
    auto m_bHasAlpha = *(bool*)((uintptr_t)self + 0x2c);
    auto m_bPreMulti = *(bool*)((uintptr_t)self + 0x2d);

    QOIEncoder encoder;

    if(!encoder.encode(m_nWidth, m_nHeight, m_pData, m_bHasAlpha, m_bPreMulti))
        return true;

    std::ofstream qoiFile;
    qoiFile.open(_qoiPath, std::ios::out | std::ios::trunc | std::ios::binary);
    qoiFile.write((const char*)encoder.getEncoded(), encoder.getEncodedSize());
    qoiFile.close();

    // used instead of actual hash for better performance
    std::filesystem::last_write_time(_qoiPath, _qoiWriteTime);

    return true;
}

DWORD WINAPI mainThread(void* hModule) {
    MH_Initialize();

    auto base = reinterpret_cast<uintptr_t>(GetModuleHandle(0));
    auto cocos2dBase = reinterpret_cast<uintptr_t>(GetModuleHandle("libcocos2d.dll"));

    MH_CreateHook(reinterpret_cast<void*>(cocos2dBase + 0xc7380), CCImage_initWithImageFile_H,
        reinterpret_cast<void**>(&CCImage_initWithImageFile));

    MH_CreateHook(reinterpret_cast<void*>(cocos2dBase + 0xc6400), CCImage__initWithPngData_H,
        reinterpret_cast<void**>(&CCImage__initWithPngData));

    MH_EnableHook(MH_ALL_HOOKS);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE handle, DWORD reason, LPVOID reserved) {
    if(reason == DLL_PROCESS_ATTACH) {
        CreateThread(0, 0x100, mainThread, handle, 0, 0);
    }
    return TRUE;
}