# EQ Master Pro 1.0.0

Plugin VST3 de equalização para Windows 10/11 (x64): 24 bandas, Dynamic EQ, Mid/Side, analisador FFT,
Auto EQ, EQ Match, medidor de loudness (BS.1770 / EBU R128), True Peak, Auto Gain, A/B e 19 presets.

## Como obter o instalador (sem instalar nada no seu PC)
1. Crie um repositório no GitHub e envie esta pasta inteira (`git push`, ou "Upload files" no site).
2. Abra a aba **Actions**. O workflow *Build EQ Master Pro* roda sozinho (leva ~15-25 min na primeira vez).
3. Ao terminar, abra a execução e baixe o artefato **EQMasterPro-DIST**: dentro estão
   `EQMasterPro-Setup-x64.exe` e `BUILD-REPORT.txt`.
4. Opcional: crie uma tag `v1.0.0` e o instalador também aparece em *Releases*.

Instale com dois cliques. O plugin vai para `C:\Program Files\Common Files\VST3\EQMasterPro.vst3`.
Na DAW, rescaneie os plugins VST3.

> O instalador não é assinado digitalmente: o Windows SmartScreen pode mostrar "Windows protegeu o seu PC".
> Clique em *Mais informações* > *Executar assim mesmo*.

## Estrutura
- `Source/DSP/` - núcleo de áudio (sem JUCE): filtros, EQ, dinâmica, loudness, True Peak, Auto EQ, EQ Match, presets
- `Source/` - plugin JUCE (processador e interface)
- `Tests/dsp_tests.cpp` - 224 verificações automáticas do DSP; `Tests/plugin_tests.cpp` - 69 verificações no processador real
- `installer/EQMasterPro.iss` - script do Inno Setup
- `.github/workflows/build.yml` - compilação, testes, validação e instalador na nuvem
- `docs/` - USER_GUIDE.md, BUILD_INFO.md

## Licença
Usa o framework JUCE 8 (AGPLv3 ou licença comercial da JUCE). Se for distribuir o plugin para terceiros,
verifique os termos em https://juce.com/legal/juce-8-licence/ .
