# Tutorial: Como o cutvideo corta vídeos com libav

Este tutorial explica os conceitos da libav (as bibliotecas do FFmpeg) usados pelo `cutvideo` e percorre a função `convert()` de [main.c](main.c) passo a passo.
Os exemplos numéricos vêm de um vídeo de teste real (H.264 a 30 fps com B-frames e keyframe a cada 2 segundos, mais áudio AAC a 48 kHz), gerado e inspecionado com `ffprobe`.

## Conceitos fundamentais

### Container vs. Codec

Um arquivo de vídeo (`.mp4`, `.mov`, `.mkv`) é um **container**: um envelope que agrupa múltiplos fluxos de dados (vídeo, áudio, legendas).
O **codec** (H.264, AAC, etc.) define como esses dados são comprimidos dentro do container.

```
arquivo.mp4
├── stream 0 → vídeo H.264
├── stream 1 → áudio AAC
└── stream 2 → legenda (opcional)
```

O `cutvideo` **não decodifica nem reencoda** nada.
Ele apenas copia os pacotes comprimidos para um novo container, processo chamado de **remuxing**.

Na libav, o lado do container fica na `libavformat` (demuxer para ler, muxer para escrever) e o lado do codec fica na `libavcodec`.
Como o `cutvideo` não decodifica, ele usa da `libavcodec` apenas a struct `AVCodecParameters`, que descreve o codec de cada stream.

---

### Pacotes (AVPacket)

A unidade de transferência da libav é o `AVPacket`.
Pense nele como um envelope: ele não sabe interpretar o que está dentro (os bytes comprimidos pertencem ao codec), mas carrega metadados suficientes para o muxer saber onde e quando colocar cada pedaço no container.

Cada pacote contém:

| Campo          | Significado                                                 |
|----------------|-------------------------------------------------------------|
| `data`         | Bytes comprimidos do codec (H.264, AAC...)                  |
| `size`         | Tamanho em bytes de `data`                                  |
| `pts`          | *Presentation timestamp*: quando o frame deve aparecer      |
| `dts`          | *Decoding timestamp*: quando o decoder deve processá-lo     |
| `duration`     | Por quanto tempo esse pacote dura                           |
| `flags`        | Bit `AV_PKT_FLAG_KEY` ligado se for um keyframe (I-frame)   |
| `stream_index` | A qual stream do container pertence                         |
| `pos`          | Posição em bytes do pacote no arquivo de origem (ou `-1`)   |

#### time_base

Todos os timestamps são inteiros, contados em "ticks" da `time_base` da stream.
A `time_base` é uma fração (`AVRational`, com `num` e `den`) que diz quantos segundos vale um tick.

A `time_base` é definida pelo **container**, não pelo codec, e cada stream pode ter a sua:

| Origem                             | Stream                  | `time_base`                  |
|------------------------------------|-------------------------|------------------------------|
| MP4 gerado pelo FFmpeg             | vídeo H.264 a 30 fps    | `1/15360`                    |
| MP4 gerado pelo FFmpeg             | áudio AAC a 48 kHz      | `1/48000`                    |
| MPEG-TS (`.ts`)                    | qualquer                | `1/90000`                    |
| Constante interna `AV_TIME_BASE_Q` | -                       | `1/1000000` (microssegundos) |

Por isso o código nunca compara timestamps de streams diferentes diretamente: ele sempre converte para uma base comum antes.

#### Por que PTS e DTS são diferentes?

Um B-frame depende de um frame **posterior** a ele na ordem de exibição.
Para decodificar o B-frame, o decoder precisa já ter decodificado esse frame posterior.
Então o encoder grava os frames numa ordem diferente da ordem de exibição, e cada pacote carrega dois timestamps:

```
Ordem de exibição (PTS):       I  B  B  P  B  B  P
Ordem de decodificação (DTS):  I  P  B  B  P  B  B
```

O DTS cresce sempre na ordem em que os pacotes aparecem no arquivo.
O PTS "pula" para frente e para trás.
Para que nenhum frame seja exibido antes de ser decodificado, vale sempre `pts >= dts`, e com B-frames até o I-frame tem `pts > dts`.

Saída real do `ffprobe` no vídeo de teste (`time_base = 1/15360`, 1 frame = 512 ticks):

```
pts=61440  dts=60416  flags=K   ← I-frame: exibido em 4.000s, decodificado em 3.933s
pts=62976  dts=60928            ← P-frame: exibido em 4.100s
pts=61952  dts=61440            ← B-frame: exibido em 4.033s
pts=62464  dts=61952            ← B-frame: exibido em 4.067s
```

A diferença de 1024 ticks (2 frames) entre PTS e DTS é o atraso de reordenação causado pelos 2 B-frames.
Em streams sem B-frames (ex: H.264 *baseline*), `pts == dts` sempre.

#### Ciclo de vida de um AVPacket

O `AVPacket` segue um padrão fixo de alocação, leitura e liberação:

```c
// 1. Alocar o struct (ainda sem buffer de dados)
AVPacket *pkt = av_packet_alloc();

// 2. av_read_frame preenche pkt com uma referência a um buffer de dados
while (av_read_frame(fmt_ctx, pkt) >= 0) {

    // usa o pacote aqui...

    // 3. Solta a referência ao buffer antes da próxima leitura
    av_packet_unref(pkt);
}

// 4. Libera o struct em si
av_packet_free(&pkt);
```

Os buffers de pacote têm contagem de referências.
`av_packet_unref` não libera o struct: ele solta a referência ao buffer (que é liberado quando ninguém mais o referencia) e volta os campos para os valores padrão.
`av_packet_free` chama `av_packet_unref` e depois libera o struct.

Esquecer o `av_packet_unref` dentro do loop vaza memória a cada pacote lido.

#### Lendo e inspecionando pacotes

```c
#include <inttypes.h>

AVPacket *pkt = av_packet_alloc();

while (av_read_frame(fmt_ctx, pkt) >= 0) {
    AVStream *stream = fmt_ctx->streams[pkt->stream_index];

    // converte PTS de ticks da time_base para segundos
    double pts_sec = pkt->pts * av_q2d(stream->time_base);

    printf("stream=%d  pts=%.3fs  dts=%" PRId64 "  dur=%" PRId64 "  keyframe=%s\n",
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

`av_q2d` converte um `AVRational` para `double`.
Para `time_base = {1, 15360}`, `av_q2d` retorna `0.0000651...`, e multiplicar pelo PTS dá os segundos.

Os campos `pts`, `dts` e `duration` são `int64_t`.
Use as macros `PRId64` de `<inttypes.h>` no `printf`: `%ld` funciona no Linux 64 bits, mas gera warning no macOS, onde `int64_t` é `long long`.

Converter para `double` é bom para exibir valores.
Para cálculos, prefira `av_rescale_q`, que trabalha só com inteiros (veja os passos 7 e 8).

#### Verificando keyframe e tipo de stream

```c
// checar se é vídeo
if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {

    // checar se é keyframe
    if (pkt->flags & AV_PKT_FLAG_KEY) {
        // ponto seguro para iniciar decodificação ou corte
    }
}
```

`flags` é um bitmask com vários bits possíveis, por isso use `&` e não `==`.

#### Exemplo concreto de timestamps

Com `time_base = {1, 15360}` (vídeo do teste):

```
pkt->pts = 1.382.400 ticks
pts em segundos = 1.382.400 × (1/15360) = 90.0s

pkt->duration = 512 ticks
duração em segundos = 512 × (1/15360) ≈ 0.033s  (1 frame a 30 fps)
```

---

### Keyframes (I-frames)

O vídeo comprimido não armazena cada frame completo.
Existem três tipos de frame:

- **I-frame** (keyframe): frame completo e autossuficiente. Pode ser decodificado sozinho.
- **P-frame**: guarda só a diferença em relação a um frame anterior.
- **B-frame**: guarda a diferença em relação a frames anteriores e posteriores.

```
 ┌──────────── GOP ────────────┐┌──────── GOP ...
 I  B  B  P  B  B  P  B  B  P   I  B  B  P  ...
 ^                              ^
 keyframe                       próximo keyframe
```

O trecho que vai de um keyframe até o frame antes do próximo é chamado de **GOP** (*Group of Pictures*).
No vídeo de teste o GOP tem 60 frames, ou seja, um keyframe a cada 2 segundos (0s, 2s, 4s, 6s...).

**Consequência crítica**: sem reencodar, um clip só pode começar num keyframe.
Se começar no meio de um GOP, o decoder não tem os frames de referência e o início do vídeo aparece corrompido, congelado ou preto.

O fim do clip, por outro lado, não precisa cair num keyframe: basta parar de copiar pacotes, porque os frames copiados só dependem de frames que vieram antes deles na ordem de decodificação.

---

## O fluxo do `convert()` passo a passo

### 1. Abrir o arquivo de entrada

```c
avformat_open_input(&ifmt_ctx, input, NULL, NULL);
avformat_find_stream_info(ifmt_ctx, NULL);
```

`avformat_open_input` detecta o formato do container, lê o cabeçalho e popula `ifmt_ctx`.
`avformat_find_stream_info` lê alguns pacotes para completar as informações de codec de cada stream (necessário quando o container não declara tudo no cabeçalho).
Esses pacotes ficam em buffer e são entregues normalmente depois pelo `av_read_frame`.

---

### 2. Criar o contexto de saída e mapear streams

```c
avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output_file);
```

Como o formato não é informado (os dois `NULL`), a libav escolhe o muxer pela extensão do nome do arquivo (`.mp4`).

Para cada stream de entrada de vídeo, áudio ou legenda, o código cria uma stream equivalente na saída e copia os parâmetros do codec:

```c
out_stream = avformat_new_stream(ofmt_ctx, NULL);
avcodec_parameters_copy(out_stream->codecpar, in_codec_param);
out_stream->codecpar->codec_tag = 0;
```

O `codec_tag` é o identificador do codec dentro do container de origem (ex: um FourCC de AVI ou MKV).
Esse valor pode não ser válido em MP4, então zerá-lo deixa o muxer escolher a tag correta para o formato de saída.

O `stream_mapping[]` traduz o índice da stream de entrada para o índice na saída, marcando com `-1` as streams que serão ignoradas (dados, anexos, etc.):

```
entrada: stream[0]=vídeo  stream[1]=áudio  stream[2]=dados
mapping:          [0]=0            [1]=1            [2]=-1  (ignorado)
saída:   stream[0]=vídeo  stream[1]=áudio
```

---

### 3. Abrir o arquivo de saída e escrever o cabeçalho

```c
if (!(ofmt_ctx->flags & AVFMT_NOFILE))
    avio_open(&ofmt_ctx->pb, output_file, AVIO_FLAG_WRITE);

avformat_write_header(ofmt_ctx, NULL);
```

`avio_open` cria o arquivo no disco.
Formatos com a flag `AVFMT_NOFILE` cuidam da própria escrita e não precisam disso, por isso a checagem.

`avformat_write_header` inicializa o muxer e escreve o início do container.
Um detalhe importante: o muxer pode definir ou alterar a `time_base` de cada stream de saída aqui.
O `cutvideo` não define `out_stream->time_base`, então o muxer MP4 escolhe uma (no vídeo de teste, a stream de vídeo de saída ficou com `1/90000`, diferente dos `1/15360` da entrada).
Por isso a conversão de timestamps do passo 8 só pode usar `out_stream->time_base` depois desta chamada.

---

### 4. Seek até o ponto inicial

```c
int64_t start_ts = start_time * AV_TIME_BASE;  // segundos → microssegundos
av_seek_frame(ifmt_ctx, -1, start_ts, AVSEEK_FLAG_BACKWARD);
```

Com stream index `-1`, a libav escolhe uma stream padrão (normalmente o vídeo) e interpreta `start_ts` em unidades de `AV_TIME_BASE` (microssegundos), convertendo internamente para a `time_base` dessa stream.

`AVSEEK_FLAG_BACKWARD` pede o **keyframe mais próximo no instante pedido ou antes dele**.
Sem essa flag, o seek poderia parar num keyframe depois do `startTime` e o clip perderia o começo.
No MP4, o demuxer sabe onde estão os keyframes porque o container guarda uma tabela deles (o atom `stss`).

No vídeo de teste, pedir `startTime = "5.5"` leva ao keyframe de **4.0s**, já que os keyframes estão em 4s e 6s.

---

### 5. Loop de leitura de pacotes

```c
while (1) {
    ret = av_read_frame(ifmt_ctx, p_packet);
    if (ret < 0) break;  // fim do arquivo ou erro

    // descarta pacotes de streams ignoradas
    if (stream_mapping[p_packet->stream_index] < 0) {
        av_packet_unref(p_packet);
        continue;
    }
    p_packet->stream_index = stream_mapping[p_packet->stream_index];

    // usa DTS (ou PTS, se não houver DTS) e converte para microssegundos
    int64_t ts = (p_packet->dts != AV_NOPTS_VALUE) ? p_packet->dts : p_packet->pts;
    if (ts == AV_NOPTS_VALUE) {
        av_packet_unref(p_packet);
        continue;
    }
    int64_t pkt_us = av_rescale_q(ts, in_stream->time_base, AV_TIME_BASE_Q);

    // passos 6 a 9...
}
```

`av_read_frame` entrega um pacote por vez, intercalando as streams (alguns pacotes de vídeo, alguns de áudio, etc.) na ordem em que estão no arquivo.

`stream_index` é trocado para o índice da stream de saída, já que a numeração pode mudar quando streams são ignoradas.

O código usa o DTS como referência de tempo porque ele cresce na mesma ordem em que os pacotes são lidos.
Converter para microssegundos (`AV_TIME_BASE_Q`) coloca vídeo e áudio na mesma escala, permitindo comparar com `end_us`.
Pacotes sem nenhum timestamp (`AV_NOPTS_VALUE`) não têm como ser posicionados e são descartados.

---

### 6. Esperar pelo primeiro keyframe de vídeo

Após o seek, os primeiros pacotes lidos podem ser de áudio ou de vídeo ainda não decodificável.
O código descarta todos até encontrar o primeiro keyframe de vídeo:

```c
int64_t actual_start_us = -1;  // declarado antes do loop

if (actual_start_us < 0) {
    if (in_stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
        !(p_packet->flags & AV_PKT_FLAG_KEY)) {
        av_packet_unref(p_packet);
        continue;  // descarta pacotes até achar o keyframe
    }
    actual_start_us = pkt_us;  // ancora o início real do clip
}

if (pkt_us > end_us) {
    av_packet_unref(p_packet);
    break;  // passou do fim do clip
}
```

O DTS desse keyframe vira o `actual_start_us`, o ponto zero do clip de saída.
No vídeo de teste, é o keyframe com `pts = 4.000s` e `dts = 3.933s`, então `actual_start_us = 3933333`.

O loop termina no primeiro pacote, de qualquer stream, cujo DTS passe de `end_us`.
Ou seja, o clip cobre o intervalo `[actual_start_us, end_us]`.

---

### 7. Normalizar timestamps para começar em zero

Os timestamps originais são relativos ao início do arquivo fonte.
O clip de saída precisa começar perto de `t=0`.

```c
int64_t start_ts_stream = av_rescale_q(
    actual_start_us,       // início real em microssegundos
    AV_TIME_BASE_Q,        // base 1/1000000
    in_stream->time_base   // base desta stream, ex: 1/15360
);

if (p_packet->pts != AV_NOPTS_VALUE)
    p_packet->pts -= start_ts_stream;
if (p_packet->dts != AV_NOPTS_VALUE)
    p_packet->dts -= start_ts_stream;
```

`actual_start_us` é convertido para a `time_base` de **cada** stream, porque vídeo e áudio contam ticks em escalas diferentes.

**Exemplo concreto** com o keyframe do vídeo de teste (`time_base = 1/15360`):

```
Arquivo original:
  keyframe: PTS = 61440 (4.000s)   DTS = 60416 (3.933s)

start_ts_stream = 3933333µs em 1/15360 = 60416 ticks

Após a subtração:
  keyframe: PTS = 1024 (0.067s)    DTS = 0
```

Como a âncora é o DTS, é o DTS do keyframe que vira zero.
O PTS fica com os 2 frames de atraso dos B-frames, então o primeiro frame do clip é exibido em `0.067s` (o `ffprobe` mostra isso como `start_time=0.066667` na stream de vídeo de saída).
Esse pequeno deslocamento é normal e os players lidam com ele sem problema.
Em vídeos sem B-frames, o PTS do keyframe fica exatamente em zero.

Sem essa subtração, o primeiro frame do clip manteria o timestamp do arquivo original (4s no exemplo, ou 90s num corte mais adiante), e o player poderia mostrar uma duração errada ou uma tela parada até chegar nesse instante.

---

### 8. Reescalar para a base de tempo de saída

```c
av_packet_rescale_ts(p_packet, in_stream->time_base, out_stream->time_base);
p_packet->pos = -1;
```

Entrada e saída podem ter `time_base` diferentes (no teste, `1/15360` na entrada e `1/90000` na saída para o vídeo).
`av_packet_rescale_ts` converte `pts`, `dts` e `duration` entre as duas bases usando `av_rescale_q`, que faz a conta só com inteiros de 64 bits e arredonda para o tick mais próximo.
Quando um valor não corresponde a um número inteiro de ticks na base de saída, ele é arredondado, com erro de no máximo meio tick de saída.

O keyframe do exemplo sai com `pts = 1024 × 90000 / 15360 = 6000` e `dts = 0`, exatamente o que o `ffprobe` mostra no clip gerado.

`pos = -1` avisa o muxer que a posição em bytes do arquivo de origem não vale para o arquivo de saída.

---

### 9. Escrever e finalizar

```c
av_interleaved_write_frame(ofmt_ctx, p_packet);
```

`av_interleaved_write_frame` mantém um buffer interno para que os pacotes de todas as streams sejam gravados intercalados em ordem crescente de DTS, como o MP4 exige.

Essa função **assume a posse** do pacote: ao retornar, `p_packet` está vazio e pronto para o próximo `av_read_frame`.
Por isso o loop não chama `av_packet_unref` depois de escrever.

Ao fim do loop:

```c
av_write_trailer(ofmt_ctx);
```

`av_write_trailer` grava os pacotes que ainda estavam no buffer e finaliza o container.
No MP4, isso escreve o atom `moov`, o índice com a posição, o tamanho e os timestamps de cada pacote.
Sem ele, o arquivo não é reproduzível.

Por padrão o `moov` fica no fim do arquivo.
Para vídeos que serão tocados por streaming na web, dá para movê-lo para o início passando a opção `movflags=+faststart` para o `avformat_write_header`.

Por fim, o código libera tudo o que alocou: o pacote, o contexto de entrada, o arquivo de saída (`avio_closep`), o contexto de saída e o `stream_mapping`.

---

## Diagrama geral do fluxo

```
JSON
 └─ inputVideoPath, clips[].name, clips[].startTime, clips[].endTime
        │
        ▼
avformat_open_input()            ← abre o container de entrada
avformat_find_stream_info()      ← descobre os codecs de cada stream
        │
        ▼
avformat_alloc_output_context2() ← escolhe o muxer pela extensão (.mp4)
avformat_new_stream()
avcodec_parameters_copy()        ← replica as streams no container de saída
        │
        ▼
avio_open()                      ← cria o arquivo de saída
avformat_write_header()          ← escreve o cabeçalho e fixa as time_base de saída
        │
        ▼
av_seek_frame(BACKWARD)          ← vai para o keyframe no startTime ou antes dele
        │
        ▼
loop av_read_frame()
  ├─ pula streams ignoradas e remapeia stream_index
  ├─ descarta até o 1º keyframe de vídeo   → define actual_start_us
  ├─ para se pkt_us > end_us               → fim do clip
  ├─ PTS/DTS -= start_ts_stream            → timestamps começam em ~0
  ├─ av_packet_rescale_ts()                → converte para a time_base de saída
  └─ av_interleaved_write_frame()          → grava o pacote no arquivo de saída
        │
        ▼
av_write_trailer()               ← escreve o índice moov e finaliza o MP4
```

---

## Vendo na prática com ffprobe

O `ffprobe` (que vem com o FFmpeg) permite conferir tudo o que foi descrito acima.

Listar a `time_base` de cada stream:

```sh
ffprobe -v error -show_entries stream=index,codec_type,time_base -of compact video.mp4
```

Listar onde estão os keyframes de vídeo (útil para prever onde um corte vai começar):

```sh
ffprobe -v error -select_streams v -show_entries packet=pts_time,flags -of csv video.mp4 | grep K
```

Ver PTS, DTS e flags dos primeiros pacotes de vídeo de um clip gerado:

```sh
ffprobe -v error -select_streams v -show_entries packet=pts,dts,flags -of compact clip.mp4 | head
```

No vídeo de teste, um clip com `startTime = "5.5"` e `endTime = "9"` sai com cerca de 5.15s de duração, e não 3.5s, porque o corte recua até o keyframe de 4s.

---

## Limitações conhecidas

- **Início impreciso**: o clip começa no keyframe no `startTime` ou antes dele. Quanto maior o GOP do vídeo de origem, maior pode ser esse recuo.
- **GOP aberto**: em alguns vídeos, os B-frames logo depois de um keyframe dependem de frames do GOP anterior. Como esse GOP não é copiado, os primeiros frames do clip podem aparecer com artefatos.
- **Bordas do áudio**: o corte é decidido pelo DTS de cada pacote, e vídeo e áudio estão intercalados no arquivo. Por isso o áudio pode começar ou terminar alguns milissegundos diferente do vídeo.

---

## Por que não reencodar?

Reencodar (decodificar, processar e encodar de novo) é lento e, com codecs com perdas como H.264 e AAC, degrada a qualidade a cada geração.
O remuxing copia os bytes comprimidos diretamente:

| | Remux | Reencoding |
|---|---|---|
| Velocidade | Limitada só pela leitura e escrita em disco | Lento (CPU/GPU intenso) |
| Qualidade | Idêntica ao original | Perde qualidade a cada geração |
| Precisão do início | Keyframe no `startTime` ou antes dele | Exata, no frame |
| Precisão do fim | Pacote (≈ 1 frame) | Exata, no frame |

Para extrair clipes onde velocidade e qualidade importam, remux é a escolha certa.
A única limitação relevante é que o início do corte sempre recua até o keyframe mais próximo antes do `startTime` pedido.
