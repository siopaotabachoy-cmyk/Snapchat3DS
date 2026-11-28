// faces.h - face photos for the carousel pictures. Made by gen_faces.py - do not edit by hand.
// This starter file has NO photos. Put your photos in a "faces" folder and run:  python3 gen_faces.py
#pragma once
#include <stdint.h>

#define FACE_PX 104                      // photos are FACE_PX x FACE_PX, RGB565
#define FACE_PHOTO_COUNT 0

typedef struct { const uint16_t *rgb; float fx, fy, fw; } FacePhoto;   // fx,fy,fw = face centre and width in pixels

static const FacePhoto FACE_PHOTOS[1] = { { 0, 0.0f, 0.0f, 0.0f } };
