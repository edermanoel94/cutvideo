#include <json-c/json.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

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

int run_ffmpeg(const char *input, struct clip_t clip) {

  int pid, status;

  pid = fork();

  if (pid == 0) {
    char output[CLIP_NAME_SIZE];
    // TODO: choosing a target dir
    sprintf(output, "%s.mp4", clip.name);

    char *args[] = {"/usr/bin/ffmpeg",
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-y",
                    "-i",
                    input,
                    "-ss",
                    clip.start_time,
                    "-to",
                    clip.end_time,
                    "-c",
                    "copy",
                    output,
                    0};

    // command-line: ffmpeg -i IMG_9032.MOV -ss 00:09:06 -to 00:09:18 -c copy
    // output.mp4
    if (execv("/usr/bin/ffmpeg", args) == -1) {
      perror("execl");
      return -1;
    }
  } else {
    wait(&status);
  }

  return 0;
}

int main(int argc, const char *argv[]) {

  if (argc != 2) {
    printf("Usage: %s <input_file>\n", argv[0]);
    exit(0);
  }

  const char *filename = argv[1];
  struct json_object *root;

  root = json_object_from_file(filename);
  if (!root) {
    printf("Error to open json file.");
    return 1;
  }

  int i, root_arr_length, clips_arr_length;

  root_arr_length = json_object_array_length(root);

  struct video_t videos[root_arr_length];

  for (i = 0; i < root_arr_length; i++) {
    struct json_object *item = json_object_array_get_idx(root, i);

    struct json_object *title_obj, *input_file_obj, *clips_obj;

    json_object_object_get_ex(item, "title", &title_obj);
    json_object_object_get_ex(item, "inputFile", &input_file_obj);
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
      if (run_ffmpeg(video.input_file, video.clips[j]) == -1) {
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
