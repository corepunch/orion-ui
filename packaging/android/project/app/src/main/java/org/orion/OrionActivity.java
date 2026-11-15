package org.orion;

import android.app.NativeActivity;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

/**
 * NativeActivity with what Orion needs from Java: the soft keyboard and
 * window insets. The app runs on its own thread (android_main); the native
 * callbacks only queue messages for it.
 */
public class OrionActivity extends NativeActivity {
    static { System.loadLibrary("main"); }

    static native void nativeText(String text);
    static native void nativeKey(int keyCode);
    static native void nativeInsets(float left, float top, float right, float bottom, float keyboard);

    private InputView input;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        input = new InputView(this);
        addContentView(input, new ViewGroup.LayoutParams(1, 1));
        getWindow().getDecorView().setOnApplyWindowInsetsListener((view, insets) -> {
            reportInsets(insets);
            return view.onApplyWindowInsets(insets);
        });
    }

    private void reportInsets(WindowInsets insets) {
        float density = getResources().getDisplayMetrics().density;
        float left, top, right, bottom, keyboard;
        if (Build.VERSION.SDK_INT >= 30) {
            android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            android.graphics.Insets ime = insets.getInsets(WindowInsets.Type.ime());
            left = bars.left; top = bars.top; right = bars.right; bottom = bars.bottom;
            keyboard = Math.max(0, ime.bottom - bars.bottom);
        } else {
            left = insets.getSystemWindowInsetLeft(); top = insets.getSystemWindowInsetTop();
            right = insets.getSystemWindowInsetRight(); bottom = insets.getStableInsetBottom();
            keyboard = Math.max(0, insets.getSystemWindowInsetBottom() - bottom);
        }
        nativeInsets(left / density, top / density, right / density, bottom / density, keyboard / density);
    }

    /** Called by the platform's axSetTextInput on the app thread; type 0 is plain text. */
    public void showKeyboard(boolean visible, int type) {
        runOnUiThread(() -> {
            InputMethodManager manager = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (visible) {
                input.requestFocus();
                manager.restartInput(input);
                manager.showSoftInput(input, 0);
            } else {
                manager.hideSoftInputFromWindow(input.getWindowToken(), 0);
                input.clearFocus();
            }
        });
    }

    /**
     * A one-pixel view that owns the IME connection. Composing text is sent
     * as it changes, replacing the previous composition with backspaces, so
     * fields update while the user types.
     */
    static final class InputView extends View {
        InputView(Context context) {
            super(context);
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        @Override public boolean onCheckIsTextEditor() { return true; }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo info) {
            info.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
            info.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_ACTION_DONE;
            return new BaseInputConnection(this, false) {
                private String composing = "";

                private void replace(CharSequence text) {
                    String next = text == null ? "" : text.toString();
                    int common = 0;
                    while (common < composing.length() && common < next.length() && composing.charAt(common) == next.charAt(common)) common++;
                    for (int i = composing.codePointCount(common, composing.length()); i > 0; i--) nativeKey(KeyEvent.KEYCODE_DEL);
                    if (common < next.length()) nativeText(next.substring(common));
                    composing = next;
                }

                @Override public boolean setComposingText(CharSequence text, int position) { replace(text); return true; }
                @Override public boolean commitText(CharSequence text, int position) { replace(text); composing = ""; return true; }
                @Override public boolean finishComposingText() { composing = ""; return true; }

                @Override
                public boolean deleteSurroundingText(int before, int after) {
                    for (int i = 0; i < before; i++) nativeKey(KeyEvent.KEYCODE_DEL);
                    for (int i = 0; i < after; i++) nativeKey(KeyEvent.KEYCODE_FORWARD_DEL);
                    return true;
                }

                @Override
                public boolean sendKeyEvent(KeyEvent event) {
                    if (event.getAction() != KeyEvent.ACTION_DOWN) return true;
                    int code = event.getKeyCode();
                    if (code == KeyEvent.KEYCODE_DEL || code == KeyEvent.KEYCODE_ENTER || code == KeyEvent.KEYCODE_FORWARD_DEL) nativeKey(code);
                    else if (event.getUnicodeChar() != 0) nativeText(new String(Character.toChars(event.getUnicodeChar())));
                    return true;
                }

                @Override
                public boolean performEditorAction(int action) {
                    nativeKey(KeyEvent.KEYCODE_ENTER);
                    return true;
                }
            };
        }
    }
}
