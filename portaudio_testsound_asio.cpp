
// melody_trycatch.cpp
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <string>
#include <windows.h> // for GetDesktopWindow (Windows only)
#include <iostream>

extern "C" {
#include <portaudio.h>
#include <pa_asio.h>
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper to convert PaError to exception
inline void checkPa(PaError err, const char* where) {
    if (err != paNoError) {
        throw std::runtime_error("LOGGER:: " + std::string(where) + ": " + Pa_GetErrorText(err));
    }
}


static void dumpHostApisAndDevices()
{
    int hostCount = Pa_GetHostApiCount();
    printf("LOGGER:: Host API count = %d\n", hostCount);
    fflush(stdout);

    for (PaHostApiIndex h = 0; h < hostCount; ++h) {
        const PaHostApiInfo* hi = Pa_GetHostApiInfo(h);
        printf("LOGGER:: HostApi[%d]: name='%s' type=%d deviceCount=%d defaultOut(host)=%d\n",
               (int)h, hi->name, (int)hi->type, hi->deviceCount, (int)hi->defaultOutputDevice);
         fflush(stdout);
    }

    int devCount = Pa_GetDeviceCount();
    printf("\nLOGGER:: Global device count = %d\n", devCount);
    fflush(stdout);

    for (PaDeviceIndex d = 0; d < devCount; ++d) {
        const PaDeviceInfo* di = Pa_GetDeviceInfo(d);
        const PaHostApiInfo* hi = Pa_GetHostApiInfo(di->hostApi);
        printf("LOGGER:: Dev[%d]: '%s' hostApi='%s' type=%d outCh=%d\n",
               (int)d, di->name, hi->name, (int)hi->type, di->maxOutputChannels);
        fflush(stdout);
    }
}


struct MelodyState {
    double phase = 0.0;
    double phaseInc = 0.0;
    float amplitude = 0.4f;
    double sampleRate = 44100.0;
    int currentNote = 0;
    int framesPlayed = 0;
    int framesPerNote = 0;
    double notes[4] = {261.63, 329.63, 392.00, 523.25}; // C4, E4, G4, C5
};

// Never throw exceptions from this callback
static int audioCallback(const void* /*input*/, void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* /*timeInfo*/,
    PaStreamCallbackFlags /*statusFlags*/,
    void* userData)
{
    auto* state = static_cast<MelodyState*>(userData);
    float* out = static_cast<float*>(output);

    for (unsigned long i = 0; i < frameCount; ++i) {
        out[i] = static_cast<float>(state->amplitude * std::sin(state->phase));
        state->phase += state->phaseInc;
        if (state->phase >= 2.0 * M_PI) state->phase -= 2.0 * M_PI;

        state->framesPlayed++;
        if (state->framesPlayed >= state->framesPerNote) {
            state->framesPlayed = 0;
            state->currentNote++;
            if (state->currentNote < 4) {
                state->phaseInc = 2.0 * M_PI * state->notes[state->currentNote] / state->sampleRate;
            } else {
                return paComplete; // end of melody
            }
        }
    }
    return paContinue;
}

void PrintLastWriteTime(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        std::wcerr << L"LOGGER:: Failed to get attributes for: " << path 
                   << L" (Error " << GetLastError() << L")\n";
        return;
    }

    // Convert FILETIME → SYSTEMTIME (local time)
    FILETIME localFt;
    FileTimeToLocalFileTime(&data.ftLastWriteTime, &localFt);

    SYSTEMTIME st;
    FileTimeToSystemTime(&localFt, &st);

    std::wcout << L"LOGGER:: Last modified: "
               << st.wYear << L"-"
               << st.wMonth << L"-"
               << st.wDay << L" "
               << st.wHour << L":"
               << st.wMinute << L":"
               << st.wSecond << L"\n";
}

int main() try {
    // Initialize
    printf("LOGGER:: Starting the logger\n");
    fflush(stdout);
    std::wstring filePath = L"libportaudio.dll";
    PrintLastWriteTime(filePath);
    fflush(stdout);
    checkPa(Pa_Initialize(), "Pa_Initialize");
    
    const PaHostErrorInfo* hei = Pa_GetLastHostErrorInfo();
    if (hei) {
        printf("LOGGER:: Last host error: hostApiType=%d errorCode=%ld text=%s\n",
            (int)hei->hostApiType, (long)hei->errorCode, hei->errorText);
        fflush(stdout);
    }

    printf("LOGGER:: Init done\n");
    fflush(stdout);
    printf("LOGGER:: PortAudio initialized. Version: %s (build %d)\n",Pa_GetVersionText(), Pa_GetVersion());
    fflush(stdout);
            

    //checkPa(Pa_GetVersionText(), "Pa_GetVersionText"), checkPa(Pa_GetVersion(), "Pa_GetVersion"));
    dumpHostApisAndDevices();
    
    const double sampleRate = 44100.0;
    MelodyState state;
    state.sampleRate = sampleRate;
    state.framesPerNote = static_cast<int>(sampleRate * 1.0); // 1 second per note
    state.phaseInc = 2.0 * M_PI * state.notes[0] / sampleRate;

    // Choose output device
    PaStreamParameters outParams{};
    outParams.device = Pa_GetDefaultOutputDevice();
    if (outParams.device == paNoDevice) {
        throw std::runtime_error("LOGGER:: No default output device.");
    }

    outParams.channelCount = 1;
    outParams.sampleFormat = paFloat32;
    outParams.suggestedLatency =
        Pa_GetDeviceInfo(outParams.device)->defaultLowOutputLatency;
    outParams.hostApiSpecificStreamInfo = nullptr;

    printf("LOGGER:: Default output device: %s\n", Pa_GetDeviceInfo(outParams.device)->name);
    fflush(stdout);


    // (Optional) Check format support and throw if unsupported
    PaError fmt = Pa_IsFormatSupported(nullptr, &outParams, sampleRate);
    if (fmt != paFormatIsSupported) {
        throw std::runtime_error("LOGGER:: " + std::string("Format not supported: ") + Pa_GetErrorText(fmt));
    }

    PaStream* stream = nullptr;
    checkPa(Pa_OpenStream(&stream, nullptr, &outParams, sampleRate,
        paFramesPerBufferUnspecified, paClipOff,
        audioCallback, &state),
        "Pa_OpenStream");

    checkPa(Pa_StartStream(stream), "Pa_StartStream");

    // Wait for completion
    while (Pa_IsStreamActive(stream) == 1) {
        Pa_Sleep(100);
    }

    // Cleanup
    checkPa(Pa_StopStream(stream), "Pa_StopStream");
    checkPa(Pa_CloseStream(stream), "Pa_CloseStream");
    checkPa(Pa_Terminate(), "Pa_Terminate");

    printf("LOGGER:: Played short melody: C4-E4-G4-C5.\n");
    fflush(stdout);
    printf("LOGGER:: Ending the logger\n");
    fflush(stdout);
    return 0;

} catch (const std::exception& ex) {
    // Try to terminate PortAudio if partially initialized
    std::fprintf(stderr, "LOGGER:: Error: %s\n", ex.what());
    fflush(stdout);
    Pa_Terminate();
    return 1;
} catch (...) {
    std::fprintf(stderr, "LOGGER:: Unknown error.\n");
    fflush(stdout);
    Pa_Terminate();
    return 1;
}
