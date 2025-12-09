#include <json-c/json.h>
#include <stdio.h>
#include <unistd.h>

struct clip_t {
  const char *name;
  const char *start_time;
  const char *end_time;
};

struct video_t {
  const char *title;
  const char *input_file;
  struct clip_t *clips;
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

  int i, root_arr_length, clips_arr_length;

  const char *filename = argv[1];
  struct json_object *root;

  root = json_object_from_file(filename);
  if (!root) {
    printf("Error to open json file.");
    return 1;
  }

  root_arr_length = json_object_array_length(root);

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

    printf("\t###### -----%s----- ######\n", video_t.title);
    printf("\t%s\n", video_t.input_file);

    for (int j = 0; j < clips_arr_length; j++) {

      struct json_object *clip_item = json_object_array_get_idx(clips_obj, j);

      struct json_object *name_obj, *start_time_obj, *end_time_obj;

      json_object_object_get_ex(clip_item, "name", &name_obj);
      json_object_object_get_ex(clip_item, "startTime", &start_time_obj);
      json_object_object_get_ex(clip_item, "endTime", &end_time_obj);

      struct clip_t clip_t = {
          .name = json_object_get_string(name_obj),
          .start_time = json_object_get_string(start_time_obj),
          .end_time = json_object_get_string(end_time_obj),
      };

      printf("\t%s\n", clip_t.name);
      printf("\t%s\n", clip_t.start_time);
      printf("\t%s\n", clip_t.end_time);
    }
  }

  // free json object
  json_object_put(root);

  return 0;
}
