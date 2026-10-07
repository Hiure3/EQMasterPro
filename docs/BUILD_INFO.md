# Build info
- Versão: 1.0.0 | Alvo: Windows 10/11 x64 | Formato: VST3
- Linguagem/ferramentas: C++17, CMake >= 3.22, MSVC (Visual Studio 2022), JUCE 8.0.15 (baixado via FetchContent), Inno Setup 6
- Runtime do MSVC ligado estaticamente (não exige redistribuível instalado)

## Build local (opcional, só se quiser)
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    ISCC.exe /DBuildDir=..\build installer\EQMasterPro.iss

## Testes do DSP (qualquer SO, só precisa de um compilador C++17)
    g++ -std=c++17 -O2 Tests/dsp_tests.cpp -o dsp_tests && ./dsp_tests

## O que é validado automaticamente
Testes do DSP (filtros, EQ, Dynamic EQ, estabilidade com NaN/Inf, LUFS M/S/I/LRA, True Peak, Auto EQ, EQ Match, presets, Auto Gain),
compilação, pluginval (strictness 5, sem testes de GUI), e instalação/desinstalação silenciosa no runner do GitHub.
## O que NÃO é validado automaticamente
Carregamento em DAW real, aparência/uso da interface, teste auditivo, Windows 10 (CI usa o Windows do runner).
