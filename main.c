#include <json-c/json.h>
#include <json-c/json_object.h>
#include <stdio.h>
#include <unistd.h>

struct clip_t {
  const char *name;
  const char *start_time;
  const char *end_time;
};

struct video_t {
  const char *title;
  const char *input_video_path;
  const char *clips;
};

int run_ffmpeg(const char *input, struct clip_t clip) {

  // ffmpeg -i IMG_9032.MOV -ss 00:09:06 -to 00:09:18 -c copy output.mp4

  char *output = NULL;
  sprintf(output, "%s.mp4", clip.name);

  printf("OUTPUT: %s\n", output);

  return execl("/usr/bin/ffmpeg", "ffmpeg", "-i", input, "-ss", clip.start_time,
               "-to", clip.end_time, "-c", "copy", output, NULL);
}

int main(int argc, const char *argv[]) {

  if (argc != 2) {
    printf("Usage: %s <input_file>\n", argv[0]);
    exit(0);
  }

  int i, array_length;

  // TODO: use argv for input file
  const char *filename = argv[1];
  struct json_object *root;

  root = json_object_from_file(filename);
  if (!root) {
    printf("Error to open json file.");
    return 1;
  }

  array_length = json_object_array_length(root);

  printf("%d\n", array_length);

  for (i = 0; i < array_length; i++) {
    struct json_object *item = json_object_array_get_idx(root, i);

    struct json_object *titleObj, *input_video_pathObj, *clipsObj;

    json_object_object_get_ex(item, "title", &titleObj);
    json_object_object_get_ex(item, "inputVideoPath", &input_video_pathObj);
    json_object_object_get_ex(item, "clips", &clipsObj);

    struct video_t video_t = {
        .title = json_object_get_string(titleObj),
        .input_video_path = json_object_get_string(input_video_pathObj),
    };

    printf("\t###### -----%s----- ######\n", video_t.title);
    printf("\t%s\n", video_t.input_video_path);
  }

  // free json object
  json_object_put(root);

  return 0;
}
