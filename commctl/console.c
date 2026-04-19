#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "commctl.h"
#include "../user/text.h"
#include "../user/user.h"
#include "../user/messages.h"

#define MAX_CONSOLE_MESSAGES 32
#define MESSAGE_DISPLAY_TIME 5000  // milliseconds
#define MESSAGE_FADE_TIME 1000     // fade out duration in milliseconds
#define MAX_MESSAGE_LENGTH 256
#define MAX_CONSOLE_LINES 10      // Maximum number of lines to display at once
#define CONSOLE_PADDING 2
#define LINE_HEIGHT 8

// Console message structure
typedef struct {
  char text[MAX_MESSAGE_LENGTH];
  uint32_t timestamp;  // When the message was created
  bool active;       // Whether the message is still being displayed
} console_message_t;

// Console state
static struct {
  console_message_t messages[MAX_CONSOLE_MESSAGES];
  int message_count;
  int last_message_index;
  bool show_console;
} console = {0};

// Initialize console system
void init_console(void) {
  memset(&console, 0, sizeof(console));
  console.show_console = true;
  console.message_count = 0;
  console.last_message_index = -1;
  init_text_rendering();
}

// Print a message to the console
void conprintf(const char* format, ...) {
  va_list args;
  console_message_t* msg;
  
  // Find the next message slot
  int index = (console.last_message_index + 1) % MAX_CONSOLE_MESSAGES;
  console.last_message_index = index;
  
  // If we haven't filled the array yet, increment the count
  if (console.message_count < MAX_CONSOLE_MESSAGES) {
    console.message_count++;
  }
  
  // Get the message slot
  msg = &console.messages[index];
  
  // Format the message
  va_start(args, format);
  vsnprintf(msg->text, MAX_MESSAGE_LENGTH, format, args);
  va_end(args);
  
  // Set the timestamp
  msg->timestamp = axGetMilliseconds();
  msg->active = true;
  
  // Also print to stdout for debugging
  printf("%s\n", msg->text);
}

// Draw the console
void draw_console(void) {
  if (!console.show_console) return;
  
  uint32_t current_time = axGetMilliseconds();
  int y = CONSOLE_PADDING;
  int lines_shown = 0;
  
  // Start from the most recent message and go backwards
  for (int i = 0; i < console.message_count && lines_shown < MAX_CONSOLE_LINES; i++) {
    int msg_index = (console.last_message_index - i + MAX_CONSOLE_MESSAGES) % MAX_CONSOLE_MESSAGES;
    console_message_t* msg = &console.messages[msg_index];
    
    // Check if the message should still be displayed
    uint32_t age = current_time - msg->timestamp;
    if (age < MESSAGE_DISPLAY_TIME) {
      // Calculate alpha based on age (fade out during the last second)
      float alpha = 1.0f;
      if (age > MESSAGE_DISPLAY_TIME - MESSAGE_FADE_TIME) {
        alpha = (MESSAGE_DISPLAY_TIME - age) / (float)MESSAGE_FADE_TIME;
      }
      
      // Draw the message using small font
      // Convert alpha to color with alpha channel (format is ABGR: 0xAABBGGRR)
      uint32_t alpha_byte = (uint32_t)(alpha * 255);
      uint32_t col = (alpha_byte << 24) | 0x00FFFFFF; // white color with variable alpha
      draw_text_small(msg->text, CONSOLE_PADDING, y, col);
      
      // Move to next line
      y += LINE_HEIGHT;
      lines_shown++;
    } else {
      // Mark message as inactive
      msg->active = false;
    }
  }
}

// Clean up console resources
void shutdown_console(void) {
  // Clear console state
  memset(&console, 0, sizeof(console));
  
  // Delegate cleanup to text rendering module
  shutdown_text_rendering();
}

// Toggle console visibility
void toggle_console(void) {
  console.show_console = !console.show_console;
}

result_t win_console(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evPaint:
      draw_console();
      break;
    default:
      break;
  }
  return false;
}
