/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License.
 */


#include "FAConfig.h"
#include "FAAllocator.h"
#include "FAFsmConst.h"
#include "FAUtils.h"
#include "FAException.h"

#include <iostream>
#include <string>
#include <fstream>
#include <assert.h>
#include <dlfcn.h>


const char * __PROG__ = "";

typedef void* (__cdecl* _TLoadModelPtr)(const char *);
_TLoadModelPtr g_LoadModelPtr = NULL;

typedef const int (__cdecl* _TTextToIdsPtr)(void*, const char*, int, int32_t*,const int, const int);
_TTextToIdsPtr g_TextToIdsPtr = NULL;

typedef int (__cdecl* _TFreeModel)(void* ModelPtr);
_TFreeModel g_FreeModelPtr = NULL;

void * g_Module = NULL;


int __cdecl main (int argc, char ** argv)
{
    __PROG__ = argv [0];

    --argc, ++argv;

    ::FAIOSetup ();

    try {

        // load library and get the function pointers
        g_Module = dlopen("./libblingfiretokdll.so", RTLD_LAZY);
        if (NULL == g_Module)
        {
            std::cerr << "ERROR: Failed to load libblingfiretokdll.so" << std::endl;
            return false;
        }

        g_LoadModelPtr = (_TLoadModelPtr) dlsym(g_Module, "LoadModel");
        if (NULL == g_LoadModelPtr)
        {
            std::cerr << "ERROR: Cannot get address of LoadModel function" << std::endl;
            return false;
        }
        g_TextToIdsPtr = (_TTextToIdsPtr) dlsym(g_Module, "TextToIds");
        if (NULL == g_TextToIdsPtr)
        {
            std::cerr << "ERROR: Cannot get address of TextToIds function" << std::endl;
            return false;
        }
        g_FreeModelPtr = (_TFreeModel) dlsym(g_Module, "FreeModel");
        if (NULL == g_FreeModelPtr)
        {
            std::cerr << "ERROR: Cannot get address of FreeModel function" << std::endl;
            return false;
        }

        // tests

        void* hModel = (*g_LoadModelPtr)("bert_base_tok.bin");

        const int MaxIdCount = 128;
        int Ids [MaxIdCount];

        std::string in1 ("⭕Namaz Ke Masail  ⭕");
         int IdCount = (*g_TextToIdsPtr)(hModel, in1.c_str(), in1.length(), Ids, MaxIdCount, 100);

        for(int i = 0; i < IdCount; ++i)
        {
            std::cout << Ids[i] << ' ';
        }
        std::cout << std::endl;

        (*g_FreeModelPtr)(hModel);

        // unload the .so file
        dlclose(g_Module);

    } catch (const FAException & e) {

        const char * const pErrMsg = e.GetErrMsg ();
        const char * const pFile = e.GetSourceName ();
        const int Line = e.GetSourceLine ();

        std::cerr << "ERROR: " << pErrMsg << " in " << pFile \
            << " at line " << Line << " in program " << __PROG__ << '\n';

        return 2;

    } catch (...) {

        std::cerr << "ERROR: Unknown error in program " << __PROG__ << '\n';

        return 1;
    }

    // print out memory leaks, if any
    FAPrintLeaks(&g_alloc, std::cerr);

    return 0;
}
