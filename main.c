#include "json_object.h"
#include <stdio.h>
#include <stdlib.h>

#include <json-c/json.h>

struct Video {};

struct Clip {};

int main() {

  int val_type, i;
  json_object *root = json_object_from_file("./data.json");

  if (!root) {
    printf("Error to open json file.");
    return 1;
  }

  char *val_type_str;

  json_object_object_foreach(root, key, val) {

    printf("Key: %s", key);

    val_type = json_object_get_type(val);

    switch (val_type) {
    case json_type_null:
      val_type_str = "val is NULL";
      break;

    case json_type_boolean:
      val_type_str = "val is a boolean";
      break;

    case json_type_double:
      val_type_str = "val is a double";
      break;

    case json_type_int:
      val_type_str = "val is an integer";
      break;

    case json_type_string:
      val_type_str = "val is a string";
      break;

    case json_type_object:
      val_type_str = "val is an object";
      break;

    case json_type_array:
      val_type_str = "val is an array";
      break;
    }

    printf("%s", val_type_str);
  }

  return 0;
}
