#pragma once

#include "date.h"
#include "draw/frame.h"
#include "phrase.h"
#include "weather.h"

struct drawing;

struct drawing *draw_open(const char *font_dir); // nullptr when the fonts can't be loaded
void draw_close(struct drawing *drawing);

struct draw_input {
    const char *phrase;       // the wanted phrase, nullptr or "" for none
    const char *glass_phrase; // drawn when the wanted one is too long, nullptr for none
    struct date date;
    struct weather_view weather;
    bool show_greeting;
    enum greeting_icon greeting_icon;
    const char *greeting;
};

struct draw_result {
    bool ok;
    bool too_long;          // the wanted phrase doesn't fit even at the smallest size
    bool drew_glass_phrase; // the glass phrase is on the frame instead
    bool phrase_drawn;      // the wanted or the glass phrase is on the frame
    int missing_glyphs;     // in the phrase drawn
};

// call it from one thread only, as Pango isn't thread-safe
struct draw_result draw(struct drawing *drawing, const struct draw_input *input, struct frame *out);
