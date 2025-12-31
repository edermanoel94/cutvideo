#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/wait.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <libavutil/mem.h>
#include <libavutil/timestamp.h>

#include <json-c/json.h>

#if __APPLE__
#define FFMPEG_BIN "/opt/homebrew/bin/ffmpeg"
#elif __linux__ || __unix__ || defined(_POSIX_VERSION)
#define FFMPEG_BIN "/usr/bin/ffmpeg"
#else
#error "Unknown compiler"
#endif

#pragma message FFMPEG_BIN

#define CLIP_NAME_SIZE 256

struct clip_t {
  const char *name;
  const char *start_time;
  const char *end_time;
};

struct video_t {
  const char *title;
  const char *input_file;
  struct clip_t *clips;

  int clips_size;
};

static void logging(const char *fmt, ...);
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt,
                       const char *tag);
static double parse_time(const char *time_str);

int run_ffmpeg(const char *input, struct clip_t clip);
int convert(const char *input, struct clip_t clip);

int main(int argc, const char *argv[]) {

  if (argc != 2) {
    printf("Usage: %s <input_file>\n", argv[0]);
    exit(0);
  }

  const char *filename = argv[1];
  struct json_object *root;

  root = json_object_from_file(filename);
  if (!root) {
    logging("Error to open json file");
    return 1;
  }

  int i, root_arr_length, clips_arr_length;

  root_arr_length = json_object_array_length(root);

  struct video_t videos[root_arr_length];

  for (i = 0; i < root_arr_length; i++) {
    struct json_object *item = json_object_array_get_idx(root, i);

    struct json_object *title_obj, *input_file_obj, *clips_obj;

    // if name is not matched with json name, should given error
    json_object_object_get_ex(item, "title", &title_obj);
    json_object_object_get_ex(item, "inputVideoPath", &input_file_obj);
    json_object_object_get_ex(item, "clips", &clips_obj);

    struct video_t video_t = {
        .title = json_object_get_string(title_obj),
        .input_file = json_object_get_string(input_file_obj),
    };

    json_object_object_get_ex(item, "clips", &clips_obj);
    clips_arr_length = json_object_array_length(clips_obj);

    struct clip_t *clips =
        (struct clip_t *)malloc(clips_arr_length * sizeof(struct clip_t));

    for (int j = 0; j < clips_arr_length; j++) {

      struct json_object *clip_item = json_object_array_get_idx(clips_obj, j);

      struct json_object *name_obj, *start_time_obj, *end_time_obj;

      json_object_object_get_ex(clip_item, "name", &name_obj);
      json_object_object_get_ex(clip_item, "startTime", &start_time_obj);
      json_object_object_get_ex(clip_item, "endTime", &end_time_obj);

      struct clip_t clip = {
          .name = json_object_get_string(name_obj),
          .start_time = json_object_get_string(start_time_obj),
          .end_time = json_object_get_string(end_time_obj),
      };

      clips[j] = clip;
    }

    video_t.clips = clips;
    video_t.clips_size = clips_arr_length;

    videos[i] = video_t;

    // ????
    clips = NULL;
  }

  int videos_length = sizeof(videos) / sizeof(struct video_t);

  for (int i = 0; i < videos_length; i++) {

    struct video_t video = videos[i];

    for (int j = 0; j < video.clips_size; j++) {
      if (convert(video.input_file, video.clips[j]) == -1) {
        break;
      }
    }

    // Using valgrind, not leaking memory, so its correct!
    free(video.clips);
  }

  // free root json object
  json_object_put(root);

  return 0;
}

int run_ffmpeg(const char *input, struct clip_t clip) {

  int pid, status;

  pid = fork();

  if (pid == 0) {
    char output[CLIP_NAME_SIZE];
    // TODO: choosing a target dir
    sprintf(output, "%s.mp4", clip.name);

    char *args[] = {FFMPEG_BIN,
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-y",
                    "-i",
                    (char *)input,
                    "-ss",
                    (char *)clip.start_time,
                    "-to",
                    (char *)clip.end_time,
                    "-c",
                    "copy",
                    output,
                    NULL};

    if (execv(FFMPEG_BIN, args) == -1) {
      perror("execl");
      return -1;
    }
  } else {
    wait(&status);
  }

  return 0;
}

int convert(const char *input, struct clip_t clip) {

  AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
  AVPacket *p_packet = NULL;
  int ret, i;
  int stream_index = 0;
  int *stream_mapping = NULL;
  int stream_mapping_size = 0;

  p_packet = av_packet_alloc();

  if (!p_packet) {
    printf("Could not allocate AVPacket\n");
    return -1;
  }

  if ((ret = avformat_open_input(&ifmt_ctx, input, NULL, NULL)) < 0) {
    printf("Could not open input file '%s'\n", input);
    return -1;
  }

  if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
    printf("Failed to retrieve input stream information\n");
    return -1;
  }

  av_dump_format(ifmt_ctx, 0, input, 0);

  char output_file[CLIP_NAME_SIZE];
  sprintf(output_file, "%s.mp4", clip.name);

  avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output_file);

  if (!ofmt_ctx) {
    printf("Failed to create ouput file context\n");
    return -1;
  }

  stream_mapping_size = ifmt_ctx->nb_streams;
  stream_mapping = av_calloc(stream_mapping_size, sizeof(*stream_mapping));

  if (!stream_mapping) {
    printf("Failed to calloc stream_mapping\n");
    return -1;
  }

  for (i = 0; i < ifmt_ctx->nb_streams; i++) {

    AVStream *out_stream;
    AVStream *in_stream = ifmt_ctx->streams[i];
    AVCodecParameters *in_codec_param = in_stream->codecpar;

    // marking streams that aren't video, audio or subtitle, so we can skip then
    // after
    if (in_codec_param->codec_type != AVMEDIA_TYPE_AUDIO &&
        in_codec_param->codec_type != AVMEDIA_TYPE_VIDEO &&
        in_codec_param->codec_type != AVMEDIA_TYPE_SUBTITLE) {
      stream_mapping[i] = -1;
      continue;
    }

    stream_mapping[i] = stream_index++;

    // initialize a stream for the output file
    out_stream = avformat_new_stream(ofmt_ctx, NULL);

    if (!out_stream) {
      printf("Failed allocating output stream\n");
      return -1;
    }

    // dst, src
    ret = avcodec_parameters_copy(out_stream->codecpar, in_codec_param);

    if (ret < 0) {
      printf("Failed to copy codec parameters\n");
      return -1;
    }

    out_stream->codecpar->codec_tag = 0;
  }

  av_dump_format(ofmt_ctx, 0, output_file, 1);

  if (!(ofmt_ctx->flags & AVFMT_NOFILE)) {

    ret = avio_open(&ofmt_ctx->pb, output_file, AVIO_FLAG_WRITE);

    if (ret < 0) {
      printf("Could not open output file '%s'\n", output_file);
      return -1;
    }
  }

  ret = avformat_write_header(ofmt_ctx, NULL);

  if (ret < 0) {
    printf("Could not write the header\n");
    return -1;
  }

  double start_time = parse_time(clip.start_time);
  double end_time = parse_time(clip.end_time);

  int64_t start_us = (int64_t)(start_time * AV_TIME_BASE);
  int64_t end_us = (int64_t)(end_time * AV_TIME_BASE);

  logging("start_time: %lf, end_time %lf", start_us, end_us);

  // convert custom time to timestamp
  // Ex.: 00:10:00 -> 10sec
  int64_t start_ts = start_time * AV_TIME_BASE;
  av_seek_frame(ifmt_ctx, -1, start_ts, AVSEEK_FLAG_BACKWARD);

  // copy packets
  while (1) {
    AVStream *in_stream, *out_stream;

    ret = av_read_frame(ifmt_ctx, p_packet);

    if (ret < 0) {
      break;
    }

    in_stream = ifmt_ctx->streams[p_packet->stream_index];

    if (p_packet->stream_index >= stream_mapping_size ||
        stream_mapping[p_packet->stream_index] < 0) {
      av_packet_unref(p_packet);
      continue;
    }

    p_packet->stream_index = stream_mapping[p_packet->stream_index];
    out_stream = ofmt_ctx->streams[p_packet->stream_index];

    int64_t ts =
        (p_packet->dts != AV_NOPTS_VALUE) ? p_packet->dts : p_packet->pts;

    if (ts == AV_NOPTS_VALUE) {
      av_packet_unref(p_packet);
      continue;
    }

    double pkt_us = av_rescale_q(ts, in_stream->time_base, AV_TIME_BASE_Q);

    if (pkt_us < start_us) {
      av_packet_unref(p_packet);
      continue;
    }

    if (pkt_us > end_us) {
      av_packet_unref(p_packet);
      break;
    }

    int64_t start_ts_stream =
        av_rescale_q(start_us, AV_TIME_BASE_Q, in_stream->time_base);

    if (p_packet->pts != AV_NOPTS_VALUE)
      p_packet->pts -= start_ts_stream;
    if (p_packet->dts != AV_NOPTS_VALUE)
      p_packet->dts -= start_ts_stream;

    log_packet(ifmt_ctx, p_packet, "in");

    av_packet_rescale_ts(p_packet, in_stream->time_base, out_stream->time_base);
    p_packet->pos = -1;

    log_packet(ofmt_ctx, p_packet, "out");

    ret = av_interleaved_write_frame(ofmt_ctx, p_packet);
    if (!out_stream) {
      fprintf(stderr, "Failed allocating output stream\n");
      return -1;
    }
  }

  av_write_trailer(ofmt_ctx);

  // END, cleaning memory
  av_packet_free(&p_packet);
  avformat_close_input(&ifmt_ctx);

  if (ofmt_ctx && !(ofmt_ctx->flags & AVFMT_NOFILE)) {
    avio_closep(&ofmt_ctx->pb);
  }

  avformat_free_context(ofmt_ctx);
  av_freep(&stream_mapping);

  return 0;
}

// Parses a time string to decimal seconds. Accepted formats:
//   "90.5"        -> 90.5  (plain decimal seconds)
//   "1:30"        -> 90.0  (MM:SS)
//   "1:30.5"      -> 90.5  (MM:SS.ms)
//   "1:00:30.5"   -> 3630.5 (HH:MM:SS.ms)
static double parse_time(const char *time_str) {
  double a = 0, b = 0, c = 0;
  int parts = sscanf(time_str, "%lf:%lf:%lf", &a, &b, &c);
  if (parts == 3)
    return a * 3600.0 + b * 60.0 + c;
  if (parts == 2)
    return a * 60.0 + b;
  return a;
}

static void logging(const char *fmt, ...) {
  va_list args;
  fprintf(stderr, "LOG: ");
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

// AVPacket->pts: Presentation timestamp (quando deve ser exibido)
// AVPacket->dts: Decoding timestamp (ordem de decodificao)
// AVPacket->duration: Duracao do pacote
// AVPacket->time_base: unidade de tempo daquela stream
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt,
                       const char *tag) {
  double pkt_time =
      pkt->pts * av_q2d(fmt_ctx->streams[pkt->stream_index]->time_base);
  printf("[%s] segundos de stream: %lf\n", tag, pkt_time);
}
