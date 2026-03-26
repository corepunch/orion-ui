// Tool registry – static array of tool pointers

extern tool_t tool_pencil;
extern tool_t tool_brush;
extern tool_t tool_eraser;
extern tool_t tool_fill;

const tool_t *tools[] = {
  &tool_pencil,
  &tool_brush,
  &tool_eraser,
  &tool_fill,
};
