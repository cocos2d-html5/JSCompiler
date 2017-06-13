//
//  main.cpp
//  jsbcc
//
//  Created by sun zhuoshi on 4/28/13.
//  Copyright (c) 2013 sunzhuoshi. All rights reserved.
//

#include <iostream>
#include <sstream>

#include <stdio.h>

#ifdef WIN32
#include <Winsock2.h>
#define STDIN_FILENO 0
#else
#include <unistd.h>
#include <sys/select.h>
#endif

#include "jsapi.h"
#include "jsfriendapi.h"
#include "js/Initialization.h"

#ifdef WIN32
const char *USAGE = "Usage: jsbcc input_js_file [byte_code_file]";
#else
const char *USAGE = "Usage: jsbcc input_js_file [byte_code_file]\n"\
                    "       Or\n"\
                    "       ls *.js | jsbcc -p";
#endif
const char *BYTE_CODE_FILE_EXT = ".jsc";

enum ErrorCode {
    EC_OK = 0,
    EC_ERROR = 1
};

void ReportError(JSContext *cx, JSErrorReport *report) {
    
    if (cx && report)
    {
        std::string fileName = report->filename ? report->filename : "<no filename=\"filename\">";
        int32_t lineno = report->lineno;
        std::string msg = report->message().c_str();
        
        std::cerr << "Error! " << fileName.c_str() << " line:" << lineno << " msg: " << msg.c_str() << std::endl;
        
        // Should clear pending exception, otherwise it will trigger infinite loop
        if (JS_IsExceptionPending(cx)) {
            JS_ClearPendingException(cx);
        }
    }
    
}

static const JSClassOps global_classOps = {
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr,
    nullptr,
    nullptr, nullptr, nullptr, JS_GlobalObjectTraceHook
};
static const JSClass global_class = {
    "global",
    JSCLASS_GLOBAL_FLAGS,
    &global_classOps
};

bool WriteFile(const std::string &filePath, void *data, uint32_t length) {
    FILE *file = fopen(filePath.c_str(), "wb");
    if (file) {
        size_t ret = fwrite(data, 1, length, file);
        fclose(file);
        if (ret == length) {
            return true;
        }
    }
    return false;
}

std::string RemoveFileExt(const std::string &filePath) {
    size_t pos = filePath.rfind('.');
    if (0 < pos) {
        return filePath.substr(0, pos);
    }
    else {
        return filePath;
    }
}

bool CompileFile(const std::string &inputFilePath, const std::string &outputFilePath) {
    bool result = false;
    std::string ofp;
    if (!outputFilePath.empty()) {
        ofp = outputFilePath;
    }
    else {
        ofp = RemoveFileExt(inputFilePath) + BYTE_CODE_FILE_EXT;
    }
    
    if (!JS_Init())
    {
        return false;
    }

    JSContext *cx = JS_NewContext(JS::DefaultHeapMaxBytes);
    if (nullptr == cx)
    {
        return false;
    }
    
    JS_SetGCParameter(cx, JSGC_MAX_BYTES, 0xffffffff);
    JS_SetGCParameter(cx, JSGC_MODE, JSGC_MODE_INCREMENTAL);
    JS_SetNativeStackQuota(cx, 500000);
    JS_SetFutexCanWait( cx);
    JS_SetDefaultLocale(cx, "UTF-8");
    JS::SetWarningReporter(cx, &ReportError);
    
    if (!JS::InitSelfHostedCode(cx))
    {
        return false;
    }
    
    JS_BeginRequest(cx);
    
    JS::CompartmentOptions options;
    options.behaviors().setVersion(JSVERSION_LATEST);
    options.creationOptions().setSharedMemoryAndAtomicsEnabled(true);
    
    JS::ContextOptionsRef(cx)
        .setIon(true)
        .setBaseline(true)
        .setAsmJS(true)
        .setNativeRegExp(true);
    
    JS::RootedObject global(cx, JS_NewGlobalObject(cx, &global_class, nullptr, JS::DontFireOnNewGlobalHook, options));
    
    JSCompartment *oldCompartment = JS_EnterCompartment(cx, global);
    
    std::cout << "Input file: " << inputFilePath << std::endl;
    
    if (JS_InitStandardClasses(cx, global)) {
        
        JS_FireOnNewGlobalObject(cx, global);
        
        JS::CompileOptions op(cx);
        op.setUTF8(true);
        op.setSourceIsLazy(true);
        op.setFileAndLine(inputFilePath.c_str(), 1);
        
        std::cout << "Compiling ..." << std::endl;
        
        JS::RootedScript script(cx);
        bool ok = JS::Compile(cx, op, inputFilePath.c_str(), &script);
        
        if (ok) {
            std::cout << "Encoding ..." << std::endl;
            
            JS::TranscodeBuffer buffer;
            JS::TranscodeResult encodeResult = JS::EncodeScript(cx, buffer, script);
            
            if (encodeResult == JS::TranscodeResult::TranscodeResult_Ok)
            {
                if (WriteFile(ofp, buffer.extractRawBuffer(), (uint32_t)buffer.length())) {
                    std::cout << "Done! " << "Output file: " << ofp << std::endl;
                    result = true;
                }
            }
        }
        else
        {
            std::cout << "Compiled " << inputFilePath << " fails!" << std::endl;
        }
    }
    else
    {
        std::cout << "JS_InitStandardClasses failed! " << std::endl;
    }
    
    if (cx) {
        JS_LeaveCompartment(cx, oldCompartment);
        JS_EndRequest(cx);
        JS_DestroyContext(cx);
        JS_ShutDown();
        cx = nullptr;
    }
    
    return result;
}

int main(int argc, const char * argv[])
{
    std::string inputFilePath, outputFilePath;
    if (1 == argc) {
        std::cerr << USAGE << std::endl;
        return EC_ERROR;
    }
    else {
        if (1 < argc) {            
            if (std::string(argv[1]) == "-p") { // pipe mode
                fd_set fds;
                FD_ZERO (&fds);
                FD_SET (STDIN_FILENO, &fds);
                int result = select (STDIN_FILENO + 1, &fds, NULL, NULL, NULL); // infinite wait
                if (result) { // STDIN ready to read
                    std::string line;
                    while (std::getline(std::cin, line)) {
                        if (!line.empty()) {
                            CompileFile(line, "");
                        }
                    }
                    return EC_OK;
                }
                else {
                    std::cerr << "Failed to read from pipe" << std::endl;
                    return EC_ERROR;
                }
            }
            else {
                inputFilePath = argv[1];
            }
        }
        if (2 < argc) {
            outputFilePath = argv[2];
        }
        if (CompileFile(inputFilePath, outputFilePath)) {
            return EC_OK;
        }
        return EC_ERROR;
    }
}
