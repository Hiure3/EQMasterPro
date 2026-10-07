# EQ Master Pro - Guia do usuário

## Gráfico (EQ + espectro)
- **Clique** num ponto vazio: cria uma banda (frequência = X, ganho = Y). **Arraste** para mover.
- **Roda do mouse** sobre o ponto: altera o Q. **Botão direito**: menu (apagar, tipo de filtro, canal, Dynamic EQ).
- **Delete/Backspace**: apaga a banda selecionada. Anel branco no ponto = banda dinâmica.
- Escala esquerda: ganho do EQ (+-24 dB). Escala direita: nível do espectro (dBFS).
- Barra superior: tamanho da FFT (1024-16384), fonte (Pre/Post EQ), AVG (suavização), DECAY (queda do peak hold).

## Bandas (24)
Selecione pelos botões numerados. Cada banda: Enable, tipo (Bell, Low/High Shelf, Low/High Pass, Notch, Band Pass, Tilt, All Pass),
Freq, Gain, Q e canal (Stereo, Mid, Side, Left, Right).
**Dynamic EQ**: Threshold, Attack, Release, Range, Ratio. Range positivo reduz o ganho da banda quando o sinal passa do threshold
(ex.: de-esser); Range negativo aumenta (expansão).

## Auto EQ
Toque o áudio e clique **AUTO EQ**: 5 s de análise do sinal de entrada. A sugestão aparece tracejada (cortes de ressonâncias,
correções suaves de balanço tonal; no máximo 8 bandas, cortes até 6 dB e realces até 3 dB). **ACCEPT** cria as bandas em slots livres; **REJECT** descarta.

## EQ Match
1. Toque o seu áudio e clique **CAPTURE CURRENT** (clique de novo para parar).
2. Toque o áudio de referência e clique **CAPTURE REFERENCE** (e pare).
3. **MATCH** calcula a curva (até 9 bandas, moderada, ignora diferença de volume). **ACCEPT/REJECT**.
Dica: carregue o preset Default antes, pois ACCEPT adiciona bandas às já existentes.

## Loudness
LUFS-M (400 ms), LUFS-S (3 s), LUFS-I (integrado com gating), LRA, True Peak L/R/MAX (dBTP, 4x), RMS, Peak, CLIP COUNT.
Escolha o alvo (-24 ... -10 ou CUSTOM) e veja TARGET / CURRENT / DELTA. **RESET** zera integrado, LRA, peak e clips.
O histórico mostra 5 minutos de M, S, I e True Peak. As medições são feitas na saída do plugin.

## Outros
- **AUTO GAIN**: compensa a mudança de nível causada pelas bandas de ganho (estimada com ruído rosa) e nunca ultrapassa 0 dBFS.
- **A / B**: duas configurações completas para comparar. **BYPASS**: bypass real com crossfade de 10 ms.
- **PRESETS**: 19 presets de fábrica (alguns também ajustam o alvo de loudness).
- Redimensione a janela arrastando o canto inferior direito.
