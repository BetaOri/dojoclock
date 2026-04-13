#include <Arduino.h>
#include "htmlStyle.h"
//<link rel="preload" href="/style.css?v=4" as="style"><link rel="stylesheet" href="/style.css?v=4"> // To make iPhone get new style sheet, not used cached.
const String HTML_PARTA = R"raw(
<!DOCTYPE html><html><head><meta charset="UTF-8"><link rel="preload" href="/style.css" as="style"><link rel="stylesheet" href="/style.css"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>
)raw";

const String HTML_PARTB = R"raw(
</title>
</head><body><div class='container'><h1>
)raw";

const String HTML_FOOTER = R"raw(
</body></html>
)raw";
