# Tutorial: Como o cutvideo corta vídeos com libav

## Conceitos fundamentais

### Container vs. Codec

Um arquivo de vídeo (`.mp4`, `.mov`, `.mkv`) é um **container**: um envelope que agrupa múltiplos fluxos de dados — vídeo, áudio, legendas. O **codec** (H.264, AAC, etc.) define como esses dados são comprimidos dentro do container.

```
arquivo.mp4
├── stream 0 → vídeo H.264
├── stream 1 → áudio AAC
└── stream 2 → legenda (opcional)
```

O `cutvideo` **não decodifica nem re-encoda** nada. Ele apenas remonta os pacotes comprimidos em um novo container — processo chamado de **remuxing**.

---

### Pacotes (AVPacket)

A unidade de transferência da libav é o `AVPacket`. Pense nele como um envelope: ele não sabe o que está dentro (os bytes comprimidos pertencem ao codec), mas carrega metadados suficientes para o muxer saber onde e quando colocar cada pedaço no container.

Cada pacote contém:

| Campo          | Significado                                               |
|----------------|-----------------------------------------------------------|
| `data`         | Bytes comprimidos do codec (H.264, AAC...)                |
| `size`         | Tamanho em bytes de `data`                                |
| `pts`          | *Presentation timestamp* — quando o frame deve aparecer   |
| `dts`          | *Decoding timestamp* — quando o decoder deve processar    |
| `duration`     | Por quanto tempo esse pacote dura                         |
| `flags`        | Bit `AV_PKT_FLAG_KEY` marcado se for um keyframe (I-frame)|
| `stream_index` | A qual stream do container pertence                       |

Todos os timestamps são inteiros na unidade `time_base` da stream (ex: `1/90000` segundos para vídeo H.264).

**Por que PTS e DTS são diferentes?**

Em vídeos com B-frames, a ordem de decodificação difere da ordem de exibição. O decoder precisa receber o frame B antes de exibi-lo, mas só pode decodificá-lo depois dos frames de referência. Por isso existem dois timestamps:

```
Ordem de exibição (PTS): I  B  B  P  B  B  P
Ordem de decodificação (DTS): I  P  B  B  P  B  B
                              ^
                    I-frame: PTS == DTS
```

Em streams sem B-frames (ex: H.264 baseline), `pts == dts` sempre.

#### Ciclo de vida de um AVPacket

O `AVPacket` segue um padrão fixo de alocação → leitura → liberação:

```c
// 1. Alocar o struct (não aloca data ainda)
AVPacket *pkt = av_packet_alloc();

// 2. av_read_frame preenche pkt->data (referência contada)
while (av_read_frame(fmt_ctx, pkt) >= 0) {

    // usa o pacote aqui...

    // 3. Libera a referência ao buffer antes da próxima leitura
    av_packet_unref(pkt);
}

// 4. Libera o struct em si
av_packet_free(&pkt);
```

`av_packet_unref` não libera o struct — apenas decrementa o contador de referências do buffer interno e zera os campos. `av_packet_free` libera o struct e chama `unref` internamente.

#### Lendo e inspecionando pacotes

```c
AVPacket *pkt = av_packet_alloc();

while (av_read_frame(fmt_ctx, pkt) >= 0) {
    AVStream *stream = fmt_ctx->streams[pkt->stream_index];

    // converte PTS de time_base para segundos
    double pts_sec = pkt->pts * av_q2d(stream->time_base);

    printf("stream=%d  pts=%.3fs  dts=%ld  dur=%ld  keyframe=%s\n",
        pkt->stream_index,
        pts_sec,
        pkt->dts,
        pkt->duration,
        (pkt->flags & AV_PKT_FLAG_KEY) ? "sim" : "não"
    );

    av_packet_unref(pkt);
}

av_packet_free(&pkt);
```

`av_q2d` converte um `AVRational` (fração `{num, den}`) para `double`. Para `time_base = {1, 90000}`, `av_q2d` retorna `0.000011...` e multiplicar pelo PTS dá os segundos.

#### Verificando keyframe e tipo de stream

```c
// checar se é vídeo
if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {

    // checar se é I-frame
    if (pkt->flags & AV_PKT_FLAG_KEY) {
        // ponto seguro para iniciar decodificação ou corte
    }
}
```

`AV_PKT_FLAG_KEY` é um bitmask — use `&`, não `==`.

#### Exemplo concreto de timestamps

Com `time_base = {1, 90000}` (típico de H.264):

```
pkt->pts = 8.100.000 ticks
pts em segundos = 8.100.000 × (1/90000) = 90.0s

pkt->duration = 3000 ticks
duração em segundos = 3000 × (1/90000) ≈ 0.033s  (≈ 1 frame a 30fps)
```

---

### Keyframes (I-frames)

O vídeo comprimido não armazena cada frame completo. Existem três tipos:

- **I-frame** (keyframe): frame completo, auto-suficiente. Pode ser decodificado sozinho.
- **P-frame**: depende do I-frame ou P-frame anterior.
- **B-frame**: depende de frames anteriores e posteriores.

```
I  P  P  B  P  B  B  I  P  P  ...
^                    ^
keyframe            próximo keyframe (GOP = Group of Pictures)
```

**Consequência crítica**: você só pode começar um clip num keyframe. Se começar no meio de um GOP, o decoder não tem os dados necessários e o vídeo fica corrompido ou em preto.

---

## O fluxo do `convert()` passo a passo

### 1. Abrir o arquivo de entrada

```c
avformat_open_input(&ifmt_ctx, input, NULL, NULL);
avformat_find_stream_info(ifmt_ctx, NULL);
```

`avformat_open_input` lê o cabeçalho do container e popula `ifmt_ctx`.
`avformat_find_stream_info` lê alguns pacotes para descobrir os codecs de cada stream (necessário quando o container não declara isso no cabeçalho).

---

### 2. Criar o contexto de saída e mapear streams

```c
avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output_file);
```

Para cada stream de entrada (vídeo, áudio, legenda), o código cria uma stream equivalente na saída e copia os parâmetros do codec:

```c
avcodec_parameters_copy(out_stream->codecpar, in_codec_param);
out_stream->codecpar->codec_tag = 0;  // deixa o muxer decidir
```

O `stream_mapping[]` traduz o índice da stream de entrada para o índice na saída, pulando streams não suportadas:

```
entrada: stream[0]=vídeo  stream[1]=áudio  stream[2]=dados
mapping:          [0]=0            [1]=1            [2]=-1  (ignorado)
saída:   stream[0]=vídeo  stream[1]=áudio
```

---

### 3. Seek até o ponto inicial

```c
int64_t start_ts = start_time * AV_TIME_BASE;  // segundos → microsegundos
av_seek_frame(ifmt_ctx, -1, start_ts, AVSEEK_FLAG_BACKWARD);
```

`AVSEEK_FLAG_BACKWARD` instrui a libav a ir até o **keyframe anterior** ao timestamp pedido. Isso é necessário porque o seek num arquivo comprimido só pode parar em I-frames — o índice do container aponta apenas para eles.

O stream index `-1` significa "use o stream de referência de tempo padrão do container".

---

### 4. Esperar pelo primeiro keyframe de vídeo

Após o seek, os primeiros pacotes podem ser de áudio ou P-frames chegando antes do I-frame. O código descarta todos até encontrar o primeiro keyframe de vídeo:

```c
int64_t actual_start_us = -1;

if (actual_start_us < 0) {
    if (in_stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
        !(p_packet->flags & AV_PKT_FLAG_KEY)) {
        av_packet_unref(p_packet);
        continue;  // descarta pacotes até achar o I-frame
    }
    actual_start_us = pkt_us;  // ancora o início real do clip
}
```

O timestamp desse I-frame vira o `actual_start_us` — o ponto zero real do clip de saída.

---

### 5. Loop de cópia de pacotes

```c
while (1) {
    ret = av_read_frame(ifmt_ctx, p_packet);
    if (ret < 0) break;  // fim do arquivo ou erro

    // converte DTS para microsegundos absolutos
    int64_t pkt_us = av_rescale_q(ts, in_stream->time_base, AV_TIME_BASE_Q);

    if (pkt_us > end_us) break;  // passou do fim do clip

    // normaliza e escreve...
}
```

`av_read_frame` entrega um pacote por vez, intercalando streams (um pacote de vídeo, um de áudio, etc.). O loop copia tudo que cair no intervalo `[actual_start_us, end_us]`.

---

### 6. Normalizar timestamps para começar em zero

Este é o ponto mais sutil. Os timestamps originais são absolutos em relação ao início do arquivo fonte. O clip de saída precisa começar em `t=0`.

```c
int64_t start_ts_stream = av_rescale_q(
    actual_start_us,       // início real em microsegundos
    AV_TIME_BASE_Q,        // base 1/1000000
    in_stream->time_base   // base desta stream, ex: 1/90000
);

if (p_packet->pts != AV_NOPTS_VALUE)
    p_packet->pts -= start_ts_stream;
if (p_packet->dts != AV_NOPTS_VALUE)
    p_packet->dts -= start_ts_stream;
```

**Exemplo concreto** com `time_base = 1/90000`:

```
Arquivo original:
  keyframe em t=90s → PTS = 90 × 90000 = 8.100.000 ticks

Após subtração (actual_start = 90s):
  PTS = 8.100.000 − 8.100.000 = 0   ← clip começa em zero
```

Sem essa subtração, o player receberia um arquivo onde o primeiro frame tem PTS = 8 milhões de ticks e pensaria que o conteúdo começa em `t=90s`, exibindo vídeo em branco nos primeiros 90 segundos.

---

### 7. Reescalar para a base de tempo de saída

```c
av_packet_rescale_ts(p_packet, in_stream->time_base, out_stream->time_base);
```

Entrada e saída podem ter `time_base` diferentes. Esta função converte PTS, DTS e duration do pacote entre as duas bases com aritmética de frações exatas (sem ponto flutuante), preservando precisão total.

---

### 8. Escrever e finalizar

```c
av_interleaved_write_frame(ofmt_ctx, p_packet);
```

`av_interleaved_write_frame` faz buffer interno para garantir que os pacotes de todas as streams saiam em ordem crescente de DTS — requisito do formato MP4.

Ao fim do loop:

```c
av_write_trailer(ofmt_ctx);
```

O trailer do MP4 (o `moov` atom) contém o índice de todos os pacotes com seus offsets no arquivo. Sem ele, o arquivo não é reproduzível — players modernos conseguem recuperar parcialmente, mas buscas (`seek`) dentro do clip não funcionariam.

---

## Diagrama geral do fluxo

```
JSON
 └─ input_file, startTime, endTime, name
        │
        ▼
avformat_open_input()           ← abre container de entrada
avformat_find_stream_info()     ← descobre codecs de cada stream
        │
        ▼
avformat_alloc_output_context2()
avcodec_parameters_copy()       ← replica streams no container de saída
        │
        ▼
av_seek_frame(BACKWARD)         ← vai para I-frame anterior ao startTime
        │
        ▼
loop av_read_frame()
  ├─ descarta até 1º I-frame de vídeo   → define actual_start_us
  ├─ para se pkt_us > end_us            → fim do clip
  ├─ PTS/DTS -= actual_start_us         → timestamps começam em 0
  ├─ av_packet_rescale_ts()             → converte time_base
  └─ av_interleaved_write_frame()       → grava pacote no arquivo de saída
        │
        ▼
av_write_trailer()              ← escreve índice moov e finaliza o MP4
```

---

## Por que não re-encodar?

Re-encodar (decodificar → processar → encodar) é lento e degrada a qualidade a cada geração. O remuxing copia os bytes comprimidos diretamente:

| | Remux | Re-encoding |
|---|---|---|
| Velocidade | Quase instantâneo | Lento (CPU/GPU intenso) |
| Qualidade | Idêntica ao original | Perde qualidade a cada geração |
| Precisão do corte | Limitada ao I-frame anterior | Frame-accurate |

Para extrair clipes onde velocidade e qualidade importam, remux é a escolha certa — a única limitação é que o corte inicial sempre recua até o I-frame mais próximo antes do `startTime` pedido.
