package com.rmsl.juce;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.RectF;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Exercises Android's real EditText and the shipping JNI range implementation.
 * No JUnit/AndroidX or network dependency; invoke with am instrument -w.
 * The target sandbox contains native-lyrics-android-report.json after every run.
 */
public final class NativeLyricsInstrumentation extends Instrumentation {
    private final JSONArray checks = new JSONArray();
    private final JSONArray failures = new JSONArray();
    private final JSONObject observations = new JSONObject();
    private Activity activity;
    private long sessionId = -700;
    private String savedDocument;
    private int saveCalls;
    private int closeCalls;
    private ClipData originalClipboard;
    private boolean clipboardCaptured;

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        start();
    }

    @Override public void onStart() {
        long started = android.os.SystemClock.elapsedRealtime();
        try {
            for (String name : new String[] {"native-lyrics-android-clean.png", "native-lyrics-android-selection.png",
                    "native-lyrics-android-enter-end.png", "native-lyrics-android-enter-before.png", "native-lyrics-android-paste-menu-debug.png"})
                new File(getTargetContext().getFilesDir(), name).delete();
            Intent intent = new Intent(getTargetContext(), JuceActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            activity = startActivitySync(intent);
            waitForIdleSync();
            check(activity != null, "real JUCE activity starts");
            modelChecks();
            uiChecks();
        } catch (Throwable error) {
            failure("uncaught", error);
        } finally {
            try {
                ui(() -> {
                    NativeSongLyricsEditor.dismiss(sessionId);
                    NativeSongLyricsEditor.testObserver = null;
                    restoreClipboard();
                });
            } catch (Throwable error) { failure("cleanup", error); }
            JSONObject report = new JSONObject();
            try {
                report.put("success", failures.length() == 0);
                report.put("checkCount", checks.length());
                report.put("checks", checks);
                report.put("failures", failures);
                report.put("observations", observations);
                report.put("device", android.os.Build.MODEL);
                report.put("sdk", android.os.Build.VERSION.SDK_INT);
                report.put("elapsedMs", android.os.SystemClock.elapsedRealtime() - started);
                File target = new File(getTargetContext().getFilesDir(), "native-lyrics-android-report.json");
                try (FileOutputStream output = new FileOutputStream(target)) {
                    output.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
                }
                Bundle results = new Bundle();
                results.putString("stream", "Native lyrics: " + checks.length() + " checks, " + failures.length() + " failures\n");
                results.putString("report", target.getAbsolutePath());
                results.putString("success", Boolean.toString(failures.length() == 0));
                if (failures.length() != 0) results.putString("failures", failures.toString());
                finish(failures.length() == 0 ? Activity.RESULT_OK : Activity.RESULT_CANCELED, results);
            } catch (Throwable error) {
                Bundle results = new Bundle();
                results.putString("stream", "Could not write native lyrics report: " + error.getClass().getSimpleName());
                finish(Activity.RESULT_CANCELED, results);
            }
        }
    }

    private void modelChecks() throws Exception {
        String text = "Primera voz 😀\nSegunda línea\n\nFinal 🎹";
        JSONObject document = document(text, new JSONArray());
        JSONObject result = transform(document, operation("section", 2, 4).put("kind", "verse"));
        document = result.getJSONObject("document");
        JSONObject verse = document.getJSONArray("sections").getJSONObject(0);
        check(verse.getInt("start") == 0 && verse.getInt("length") == text.indexOf('\n'),
                "selecting a word labels its complete paragraph");
        check(document.getString("text").equals(text), "section assignment does not insert hidden text");
        check(result.getJSONArray("labels").getJSONObject(0).getString("code").equals("V1"),
                "verse numbering uses shared native labels");
        String verseId = verse.getString("id");

        int end = verse.getInt("length");
        result = transform(document, operation("replace", end, 0).put("text", "\n"));
        document = result.getJSONObject("document");
        verse = document.getJSONArray("sections").getJSONObject(0);
        check(verse.getInt("length") == end + 1 && verseId.equals(verse.getString("id")),
                "exact Return extends the section before an existing LF");
        String afterReturn = document.getString("text");
        check(afterReturn.equals(text.substring(0, end) + "\n" + text.substring(end)),
                "Return modifies only its exact UTF16 insertion range");

        result = transform(document, operation("replace", end + 1, 0).put("text", "  😀"));
        document = result.getJSONObject("document");
        verse = document.getJSONArray("sections").getJSONObject(0);
        check(verse.getInt("length") == end + 5,
                "spaces and emoji continue the same section in UTF16 units");
        check(verseId.equals(verse.getString("id")), "continuation preserves stable section ID");

        int secondStart = document.getString("text").indexOf("Segunda");
        result = transform(document, operation("section", secondStart, 1).put("kind", "chorus"));
        document = result.getJSONObject("document");
        JSONObject chorus = document.getJSONArray("sections").getJSONObject(1);
        String chorusId = chorus.getString("id");
        check(chorus.getInt("start") == secondStart && chorus.getInt("length") == "Segunda línea".length(),
                "second paragraph receives a separate nonoverlapping section");
        result = transform(document, operation("replace", 0, 0).put("text", "🎹 "));
        document = result.getJSONObject("document");
        check(document.getJSONArray("sections").getJSONObject(0).getInt("start") == 0,
                "insertion at full-card start stays inside that card");
        chorus = document.getJSONArray("sections").getJSONObject(1);
        check(chorus.getInt("start") == secondStart + 3 && chorusId.equals(chorus.getString("id")),
                "earlier Unicode insertion shifts following card without changing ID");

        String clearText = document.getString("text");
        result = transform(document, operation("clear", 0, verse.getInt("length") + 3));
        document = result.getJSONObject("document");
        check(document.getString("text").equals(clearText), "remove section retains literal lyrics");
        check(document.getJSONArray("sections").length() == 1 &&
                document.getJSONArray("sections").getJSONObject(0).getString("id").equals(chorusId),
                "remove section preserves later card identity");

        JSONObject invalid = new JSONObject(NativeSongLyricsEditor.nativeTransform(
                document("A😀B", new JSONArray()).toString(), operation("replace", 2, 0).put("text", "x").toString()));
        check(invalid.has("error"), "JNI rejects range splitting Unicode surrogate pair");

        JSONObject crlf = document("Uno\r\nDos\r\nTres", new JSONArray());
        result = transform(crlf, operation("section", 0, 5).put("kind", "verse"));
        JSONObject range = result.getJSONObject("document").getJSONArray("sections").getJSONObject(0);
        check(range.getInt("length") == 3,
                "selection ending exactly at next paragraph excludes that paragraph and CRLF");
        check(result.getJSONObject("document").getString("text").equals("Uno\r\nDos\r\nTres"),
                "CRLF remains byte-for-byte text content");

        JSONArray adjacent = new JSONArray()
                .put(section("left", "verse", 0, 4))
                .put(section("right", "chorus", 4, 3));
        result = transform(document("Uno\nDos", adjacent), operation("replace", 4, 0).put("text", "X"));
        JSONArray ranges = result.getJSONObject("document").getJSONArray("sections");
        check(ranges.getJSONObject(0).getInt("length") == 4 && ranges.getJSONObject(1).getInt("start") == 4 &&
                ranges.getJSONObject(1).getInt("length") == 4,
                "shared boundary prioritizes following card");
    }

    private void uiChecks() throws Exception {
        final int originalOrientation = onUi(() -> activity.getRequestedOrientation());
        NativeSongLyricsEditor.TestObserver observer = new NativeSongLyricsEditor.TestObserver() {
            @Override public void onSave(long id, String document) { ++saveCalls; savedDocument = document; }
            @Override public void onClose(long id) { ++closeCalls; }
        };
        ui(() -> NativeSongLyricsEditor.testObserver = observer);
        show(document("", new JSONArray()));
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            check(session.dialog.isShowing(), "native editor is presented full-screen");
            check(session.editor.getText().length() == 0 && session.editor.getHint().length() > 0,
                    "empty editor invites paste or writing without sample content");
            check(countEditTexts(session.dialog.getWindow().getDecorView()) == 1,
                    "single native EditText owns entire document");
            check(session.editor.getTextSize() / session.editor.getResources().getDisplayMetrics().scaledDensity >= 16.9f &&
                    session.editor.getTextSize() / session.editor.getResources().getDisplayMetrics().scaledDensity <= 17.1f,
                    "body uses compact 17sp typography");
            check(session.editor.getCurrentTextColor() == android.graphics.Color.parseColor("#EBEBEB"),
                    "native body text uses shared app palette");
            check(session.editor.getCustomSelectionActionModeCallback() != null,
                    "native selection retains custom Section action");
            check(session.editor.getBackground() == null || session.editor.getBackground().getOpacity() != android.graphics.PixelFormat.OPAQUE,
                    "text background does not cover native selection shading");
            assertDrawableRecreation(session.editor.getTextCursorDrawable(), "native cursor");
            assertDrawableRecreation(session.editor.getTextSelectHandle(), "native insertion handle");
            assertDrawableRecreation(session.editor.getTextSelectHandleLeft(), "native left selection handle");
            assertDrawableRecreation(session.editor.getTextSelectHandleRight(), "native right selection handle");
        });
        // Capture only while the initial dialog owns focus. Samsung's insertion
        // handle/IME popup may legitimately change focus after the following tap.
        waitForInitialDialogFocus();
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            ClipboardManager clipboard = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
            check(session.editor.hasWindowFocus(), "editor owns window focus before saving clipboard");
            originalClipboard = clipboard.getPrimaryClip();
            clipboardCaptured = true;
            clipboard.setPrimaryClip(ClipData.newPlainText("lyrics", "Hoy cantamos 😀\nUna voz nos une\n\nTu luz alumbra"));
        });
        touchEditor(false, 0);
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            check(session.dialog.isShowing() && session.editor.hasFocus(), "real tap creates native insertion handle without crashing");
        });
        touchEditor(true, 0);
        boolean menuClicked = clickNativePaste();
        observations.put("nativePasteMenuClicked", menuClicked);
        boolean pasted = menuClicked;
        if (!menuClicked) {
            diagnosePasteMenu();
            pasted = onUi(() -> active().editor.onTextContextMenuItem(android.R.id.paste));
        }
        observations.put("nativePasteInvocation", menuClicked ? "accessibility_menu" : "TextView.onTextContextMenuItem");
        check(pasted, "native Paste action is accepted");
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            check(session.editor.getText().toString().equals("Hoy cantamos 😀\nUna voz nos une\n\nTu luz alumbra"),
                    "native clipboard paste retains Unicode and line breaks");
            check(session.document.optString("text").equals(session.editor.getText().toString()),
                    "live native text and canonical document agree after paste");
            restoreClipboard();
        });
        touchEditor(true, 2);
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            check(session.editor.getSelectionEnd() > session.editor.getSelectionStart(),
                    "real long press creates native word selection handles without crashing");
            session.editor.setSelection(2, 5);
            android.view.Menu menu = new android.widget.PopupMenu(activity, session.editor).getMenu();
            android.view.ActionMode.Callback actions = session.editor.getCustomSelectionActionModeCallback();
            check(actions.onCreateActionMode(null, menu), "native selection menu accepts Section extension");
            android.view.MenuItem sectionAction = null;
            for (int i = 0; i < menu.size(); ++i)
                if ("Sección".contentEquals(menu.getItem(i).getTitle())) sectionAction = menu.getItem(i);
            check(sectionAction != null && actions.onActionItemClicked(null, sectionAction),
                    "Section action opens native picker from current selected range");
            check(session.activeAlert != null && session.activeAlert.isShowing(),
                    "section options use native Android alert picker");
            android.widget.ListView choices = session.activeAlert.getListView();
            choices.performItemClick(choices.getChildAt(1), 1, choices.getAdapter().getItemId(1));
            check(session.document.optJSONArray("sections").length() == 1,
                    "word selection creates one section");
            check(session.document.optJSONArray("sections").optJSONObject(0).optInt("length") == "Hoy cantamos 😀".length(),
                    "Section action expands selection to complete paragraph");
            check(session.editor.getText().toString().equals(session.document.optString("text")),
                    "card decorations do not enter editable text");
        });
        waitForIdleSync();
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            JSONObject section = session.document.optJSONArray("sections").optJSONObject(0);
            String id = section.optString("id");
            assertCardGeometry(session, id, "after marking first paragraph");
            session.editor.clearFocus();
            session.navigateToSection(id);
            check(session.editor.getSelectionStart() == section.optInt("start") &&
                    session.editor.getSelectionStart() == session.editor.getSelectionEnd(),
                    "circle navigation places collapsed caret rather than selecting block");
            check(!session.editor.hasFocus(), "circle navigation does not force keyboard focus");
        });
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            String lyrics = session.editor.getText().toString();
            session.applySection("verse", 0, lyrics.indexOf("\n\n"));
            int chorus = lyrics.indexOf("Tu luz");
            session.applySection("chorus", chorus, session.editor.getText().length());
            session.editor.clearFocus();
            InputMethodManager keyboard = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            keyboard.hideSoftInputFromWindow(session.editor.getWindowToken(), 0);
        });
        waitForIdleSync();
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            for (int i = 0; i < session.document.optJSONArray("sections").length(); ++i) {
                String id = session.document.optJSONArray("sections").optJSONObject(i).optString("id");
                assertCardGeometry(session, id, "after assigning section");
                assertPaintedHeaderPosition(session, id);
            }
        });
        screenshot("native-lyrics-android-clean.png");
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            session.editor.requestFocus();
            session.editor.setSelection(0, session.editor.getText().length());
        });
        screenshot("native-lyrics-android-selection.png");
        scrollUiChecks();
        continuationUiChecks(false);
        continuationUiChecks(true);
        persistenceAndLifecycleChecks(originalOrientation);
    }

    private void waitForInitialDialogFocus() {
        long deadline = android.os.SystemClock.uptimeMillis() + 4000;
        while (android.os.SystemClock.uptimeMillis() < deadline) {
            if (onUi(() -> active().editor.hasWindowFocus())) return;
            android.os.SystemClock.sleep(100);
        }
        // The caller checks once more and fails before reading/mutating clipboard.
    }

    private void assertDrawableRecreation(android.graphics.drawable.Drawable source, String label) {
        check(source != null, label + " exists");
        android.graphics.drawable.Drawable.ConstantState state = source.getConstantState();
        check(state != null, label + " exposes cloneable ConstantState for OEM editor");
        android.graphics.drawable.Drawable first = state.newDrawable().mutate();
        android.graphics.drawable.Drawable second = state.newDrawable().mutate();
        check(first != null && second != null && first != second && first != source && second != source,
                label + " recreates distinct drawable instances");
        android.graphics.Rect originalBounds = new android.graphics.Rect(source.getBounds());
        first.setBounds(1, 2, 23, 24);
        second.setBounds(31, 32, 43, 44);
        check(first.getBounds().equals(new android.graphics.Rect(1, 2, 23, 24)) &&
                second.getBounds().equals(new android.graphics.Rect(31, 32, 43, 44)) &&
                source.getBounds().equals(originalBounds), label + " clones retain independent bounds");
        Bitmap bitmap = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888);
        try { first.draw(new Canvas(bitmap)); second.draw(new Canvas(bitmap)); }
        finally { bitmap.recycle(); }
        check(source.getBounds().equals(originalBounds), label + " clones draw without changing source bounds");
    }

    private void touchEditor(boolean longPress, int offset) {
        // Distinct user gestures: a preceding tap must not turn the hold into a double tap.
        if (longPress) android.os.SystemClock.sleep(android.view.ViewConfiguration.getDoubleTapTimeout() + 80L);
        final long downTime = android.os.SystemClock.uptimeMillis();
        final float[] point = onUi(() -> {
            EditText editor = active().editor;
            android.text.Layout layout = editor.getLayout();
            int bounded = Math.max(0, Math.min(offset, editor.length()));
            int line = layout.getLineForOffset(bounded);
            android.graphics.Paint.FontMetricsInt metrics = editor.getPaint().getFontMetricsInt();
            return new float[] {editor.getTotalPaddingLeft() + layout.getPrimaryHorizontal(bounded) - editor.getScrollX() + 1,
                    editor.getTotalPaddingTop() + layout.getLineBaseline(line) + (metrics.ascent + metrics.descent) / 2f - editor.getScrollY()};
        });
        ui(() -> {
            android.view.MotionEvent event = android.view.MotionEvent.obtain(downTime, downTime,
                    android.view.MotionEvent.ACTION_DOWN, point[0], point[1], 0);
            event.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN);
            try { check(active().editor.dispatchTouchEvent(event), "native editor accepts real touch down " + longPress); }
            finally { event.recycle(); }
        });
        android.os.SystemClock.sleep(longPress ? android.view.ViewConfiguration.getLongPressTimeout() + 150L : 70L);
        ui(() -> {
            android.view.MotionEvent event = android.view.MotionEvent.obtain(downTime, android.os.SystemClock.uptimeMillis(),
                    android.view.MotionEvent.ACTION_UP, point[0], point[1], 0);
            event.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN);
            try { check(active().editor.dispatchTouchEvent(event), "native editor accepts real touch up " + longPress); }
            finally { event.recycle(); }
        });
        waitForIdleSync();
    }

    private boolean clickNativePaste() {
        String title = activity.getString(android.R.string.paste);
        android.accessibilityservice.AccessibilityServiceInfo service = getUiAutomation().getServiceInfo();
        int previousFlags = service.flags;
        service.flags |= android.accessibilityservice.AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS;
        getUiAutomation().setServiceInfo(service);
        try {
            for (int attempt = 0; attempt < 15; ++attempt) {
                for (android.view.accessibility.AccessibilityWindowInfo window : getUiAutomation().getWindows()) {
                    android.view.accessibility.AccessibilityNodeInfo root = window.getRoot();
                    if (root == null) continue;
                    try {
                        CharSequence owner = root.getPackageName();
                        if (owner == null || !(getTargetContext().getPackageName().contentEquals(owner) || "android".contentEquals(owner))) continue;
                        if (clickPasteNode(root, title, 0)) return true;
                    } finally { root.recycle(); }
                }
                android.os.SystemClock.sleep(100);
            }
            return false;
        } finally {
            service.flags = previousFlags;
            getUiAutomation().setServiceInfo(service);
            waitForIdleSync();
        }
    }

    private boolean clickPasteNode(android.view.accessibility.AccessibilityNodeInfo node, String title, int depth) {
        if (depth > 30) return false;
        boolean matches = title.contentEquals(node.getText() == null ? "" : node.getText())
                || title.contentEquals(node.getContentDescription() == null ? "" : node.getContentDescription());
        if (matches) {
            if (node.isClickable() && node.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK)) return true;
            android.view.accessibility.AccessibilityNodeInfo parent = node.getParent();
            try {
                if (parent != null && parent.isClickable() && parent.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK)) return true;
            } finally { if (parent != null) parent.recycle(); }
        }
        for (int i = 0; i < node.getChildCount(); ++i) {
            android.view.accessibility.AccessibilityNodeInfo child = node.getChild(i);
            if (child == null) continue;
            try { if (clickPasteNode(child, title, depth + 1)) return true; }
            finally { child.recycle(); }
        }
        return false;
    }

    private void diagnosePasteMenu() throws Exception {
        // Record only known editing-control labels from the target/native popup,
        // never arbitrary node text or the saved original clipboard.
        JSONArray windows = new JSONArray();
        android.accessibilityservice.AccessibilityServiceInfo service = getUiAutomation().getServiceInfo();
        int previousFlags = service.flags;
        service.flags |= android.accessibilityservice.AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS;
        getUiAutomation().setServiceInfo(service);
        try {
            for (android.view.accessibility.AccessibilityWindowInfo window : getUiAutomation().getWindows()) {
                android.view.accessibility.AccessibilityNodeInfo root = window.getRoot();
                if (root == null) continue;
                try {
                    CharSequence owner = root.getPackageName();
                    if (owner == null || !(getTargetContext().getPackageName().contentEquals(owner) || "android".contentEquals(owner))) continue;
                    JSONArray controls = new JSONArray();
                    collectEditingLabels(root, controls, 0);
                    windows.put(new JSONObject().put("package", owner.toString()).put("type", window.getType()).put("controls", controls));
                } finally { root.recycle(); }
            }
        } finally {
            service.flags = previousFlags;
            getUiAutomation().setServiceInfo(service);
        }
        observations.put("nativePasteMenuAutomationDiagnostic", windows);
        android.graphics.Rect bounds = onUi(() -> {
            android.graphics.Rect visible = new android.graphics.Rect();
            active().editor.getGlobalVisibleRect(visible);
            visible.top = Math.max(0, visible.top - active().dp(48));
            return visible;
        });
        Bitmap screen = getUiAutomation().takeScreenshot();
        if (screen != null) {
            bounds.intersect(0, 0, screen.getWidth(), screen.getHeight());
            if (!bounds.isEmpty()) {
                Bitmap appArea = Bitmap.createBitmap(screen, bounds.left, bounds.top, bounds.width(), bounds.height());
                File target = new File(getTargetContext().getFilesDir(), "native-lyrics-android-paste-menu-debug.png");
                try (FileOutputStream output = new FileOutputStream(target)) { appArea.compress(Bitmap.CompressFormat.PNG, 100, output); }
                finally { appArea.recycle(); }
                observations.put("nativePasteMenuAutomationScreenshot", target.getName());
            }
            screen.recycle();
        }
    }

    private void collectEditingLabels(android.view.accessibility.AccessibilityNodeInfo node, JSONArray result, int depth) throws Exception {
        if (depth > 30 || result.length() >= 40) return;
        String text = node.getText() == null ? "" : node.getText().toString();
        String description = node.getContentDescription() == null ? "" : node.getContentDescription().toString();
        String matched = "";
        for (String label : new String[] {activity.getString(android.R.string.paste), "Paste", "Pegar", "Sección", "Section", "Copy", "Copiar", "Cut", "Cortar", "Select all", "Seleccionar todo"})
            if (label.equals(text) || label.equals(description)) { matched = label; break; }
        if (!matched.isEmpty()) result.put(new JSONObject().put("label", matched).put("viaDescription", matched.equals(description))
                .put("class", String.valueOf(node.getClassName())).put("clickable", node.isClickable()).put("enabled", node.isEnabled()));
        for (int i = 0; i < node.getChildCount(); ++i) {
            android.view.accessibility.AccessibilityNodeInfo child = node.getChild(i);
            if (child == null) continue;
            try { collectEditingLabels(child, result, depth + 1); }
            finally { child.recycle(); }
        }
    }

    private void restoreClipboard() {
        if (!clipboardCaptured) return;
        ClipboardManager clipboard = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
        if (originalClipboard != null) clipboard.setPrimaryClip(originalClipboard);
        else clipboard.clearPrimaryClip();
        originalClipboard = null;
        clipboardCaptured = false;
    }

    private void scrollUiChecks() throws Exception {
        StringBuilder text = new StringBuilder();
        JSONArray sections = new JSONArray();
        for (int i = 0; i < 18; ++i) {
            if (i > 0) text.append("\n\n");
            int start = text.length();
            text.append("Luz y esperanza ").append(i + 1).append("\nJuntos cantamos en unidad");
            sections.put(section("scroll-" + i, i % 2 == 0 ? "verse" : "chorus", start, text.length() - start));
        }
        dismiss();
        show(document(text.toString(), sections));
        ui(() -> { active().editor.clearFocus(); active().navigateToSection("scroll-17"); });
        waitForIdleSync();
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            assertCardGeometry(session, "scroll-17", "after navigating to lower card");
            check(session.editor.getScrollY() > 0, "circle navigation scrolls to a distant lower card");
            RectF card = session.editor.cardRectForTesting("scroll-17");
            check(card.top - session.editor.getScrollY() >= 0 &&
                    card.bottom - session.editor.getScrollY() <= session.editor.getHeight(),
                    "destination card is fully visible after navigation");
            check(session.editor.getSelectionStart() == session.editor.getSelectionEnd(),
                    "lower-card navigation leaves a collapsed native caret");
            assertPaintedHeaderPosition(session, "scroll-17");
            session.navigateToSection("scroll-0");
        });
        waitForIdleSync();
        ui(() -> {
            assertCardGeometry(active(), "scroll-0", "after returning to first card");
            assertPaintedHeaderPosition(active(), "scroll-0");
        });
    }

    private void assertPaintedHeaderPosition(NativeSongLyricsEditor.EditorSession session, String id) {
        View decor = session.dialog.getWindow().getDecorView();
        Bitmap bitmap = Bitmap.createBitmap(decor.getWidth(), decor.getHeight(), Bitmap.Config.ARGB_8888);
        decor.draw(new Canvas(bitmap));
        try {
            int[] editorPosition = new int[2], decorPosition = new int[2];
            session.editor.getLocationOnScreen(editorPosition);
            decor.getLocationOnScreen(decorPosition);
            RectF card = session.editor.cardRectForTesting(id);
            int x = editorPosition[0] - decorPosition[0] + session.dp(58);
            int y = editorPosition[1] - decorPosition[1] + Math.round(card.top)
                    - session.editor.getScrollY() - session.dp(14) + session.dp(5);
            int expected = 0;
            for (int i = 0; i < session.labels.length(); ++i) {
                JSONObject label = session.labels.optJSONObject(i);
                if (id.equals(label.optString("id"))) expected = android.graphics.Color.parseColor(label.optString("color"));
            }
            check(expected != 0 && x >= 0 && x < bitmap.getWidth() && y >= 0 && y < bitmap.getHeight(),
                    "painted destination header sample is on-screen " + id);
            int actual = bitmap.getPixel(x, y);
            int distance = Math.abs(android.graphics.Color.red(actual) - android.graphics.Color.red(expected))
                    + Math.abs(android.graphics.Color.green(actual) - android.graphics.Color.green(expected))
                    + Math.abs(android.graphics.Color.blue(actual) - android.graphics.Color.blue(expected));
            check(distance < 24, "painted section header follows native scroll coordinates " + id);
        } finally { bitmap.recycle(); }
    }

    private void continuationUiChecks(boolean atEnd) throws Exception {
        String prefix = "Antes 😀\n\n";
        String body = "Tu voz me guía\nSeguimos en unidad";
        String suffix = atEnd ? "" : "\n\nOtra sección";
        String text = prefix + body + suffix;
        JSONArray sections = new JSONArray().put(section("current", "chorus", prefix.length(), body.length()));
        if (!atEnd) sections.put(section("following", "verse", prefix.length() + body.length() + 2, "Otra sección".length()));
        dismiss();
        show(document(text, sections));
        final int end = prefix.length() + body.length();
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            session.editor.requestFocus();
            session.editor.setSelection(end);
            InputConnection connection = session.editor.onCreateInputConnection(new EditorInfo());
            check(connection != null, "native IME connection is available " + atEnd);
            check(connection.commitText("\n", 1), "IME Return accepted " + atEnd);
        });
        waitForIdleSync();
        ui(() -> {
            assertCaretInsideCard(active(), "current", "immediately after Return " + atEnd);
            InputMethodManager keyboard = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            keyboard.showSoftInput(active().editor, InputMethodManager.SHOW_IMPLICIT);
        });
        screenshot(atEnd ? "native-lyrics-android-enter-end.png" : "native-lyrics-android-enter-before.png");
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            InputConnection connection = session.editor.onCreateInputConnection(new EditorInfo());
            check(connection.commitText("\n   ", 1), "IME repeated Return and spaces accepted " + atEnd);
        });
        waitForIdleSync();
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            assertCaretInsideCard(session, "current", "after repeated Return and spaces " + atEnd);
            JSONObject current = findSection(session.document, "current");
            check(current.optInt("length") == body.length() + 5,
                    "blank continuation belongs to card before typing next word " + atEnd);
            check(session.document.optString("text").equals(session.editor.getText().toString()),
                    "exact text model matches blank continuation " + atEnd);
            if (!atEnd) {
                JSONObject following = findSection(session.document, "following");
                check(following.optInt("start") == prefix.length() + body.length() + 7 &&
                        following.optInt("length") == "Otra sección".length(),
                        "following section shifts without absorbing preceding blanks");
            }
            JSONArray timeline = new JSONArray();
            android.text.TextWatcher watcher = new android.text.TextWatcher() {
                private void record(String phase, CharSequence text, int start, int before, int after) {
                    if (timeline.length() > 20) return;
                    try {
                        timeline.put(new JSONObject().put("phase", phase).put("text", text.toString())
                                .put("documentText", session.document.optString("text"))
                                .put("start", start).put("before", before).put("after", after));
                    } catch (Exception ignored) {}
                }
                public void beforeTextChanged(CharSequence text, int start, int count, int after) { record("before", text, start, count, after); }
                public void onTextChanged(CharSequence text, int start, int before, int count) { record("changed", text, start, before, count); }
                public void afterTextChanged(android.text.Editable text) { record("after", text, -1, -1, -1); }
            };
            session.editor.addTextChangedListener(watcher);
            boolean undone;
            try { undone = session.editor.onTextContextMenuItem(android.R.id.undo); }
            finally { session.editor.removeTextChangedListener(watcher); }
            check(undone, "native Undo is available " + atEnd);
            if (!session.document.optString("text").equals(session.editor.getText().toString())) {
                editingDiagnostic(session, "Undo " + atEnd);
                try { failures.put(new JSONObject().put("diagnostic", "native Undo timeline").put("events", timeline)); }
                catch (Exception ignored) {}
            }
            check(session.document.optString("text").equals(session.editor.getText().toString()),
                    "native Undo restores matching metadata text " + atEnd);
            JSONObject undoSection = findSection(session.document, "current");
            check(undoSection != null && undoSection.optInt("length") ==
                    session.editor.getText().length() - prefix.length() - suffix.length(),
                    "Undo retains exact current-card range " + atEnd);
            check(session.editor.onTextContextMenuItem(android.R.id.redo), "native Redo is available " + atEnd);
            check(session.document.optString("text").equals(session.editor.getText().toString()) &&
                    findSection(session.document, "current").optInt("length") == body.length() + 5,
                    "Redo restores blank continuation metadata " + atEnd);
            session.editor.setSelection(prefix.length() + 2, prefix.length() + 6);
            check(session.editor.getSelectionStart() == prefix.length() + 2 &&
                    session.editor.getSelectionEnd() == prefix.length() + 6,
                    "native text selection survives card presentation " + atEnd);
            session.editor.setSelection(prefix.length() + body.length() + 5);
            InputConnection connection = session.editor.onCreateInputConnection(new EditorInfo());
            check(connection.setComposingText(" cantamos", 1), "IME starts marked composition " + atEnd);
            check(android.view.inputmethod.BaseInputConnection.getComposingSpanStart(session.editor.getText()) >= 0,
                    "formatting retains native marked composition span " + atEnd);
            check(connection.setComposingText(" cantaremos juntos", 1), "IME grows marked composition " + atEnd);
            check(connection.setComposingText(" voz", 1), "IME shortens marked composition " + atEnd);
            check(connection.finishComposingText(), "IME commits marked composition " + atEnd);
            check(session.document.optString("text").equals(session.editor.getText().toString()),
                    "composition commit preserves exact native text " + atEnd);
            check(findSection(session.document, "current").optInt("length") == body.length() + 9,
                    "composition range belongs to current section " + atEnd);
        });
        waitForIdleSync();
        ui(() -> assertCaretInsideCard(active(), "current", "after composition commit " + atEnd));
    }

    private void persistenceAndLifecycleChecks(int originalOrientation) throws Exception {
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            session.requestSave();
            session.requestSave();
            check(saveCalls == 1 && savedDocument != null, "save sends canonical document once while pending");
            check(!session.editor.isEnabled(), "native editing is disabled while save is pending");
            NativeSongLyricsEditor.completeSave(sessionId, false, "Prueba de error de guardado");
        });
        waitForIdleSync();
        ui(() -> {
            NativeSongLyricsEditor.EditorSession session = active();
            check(session.dialog.isShowing(), "save failure keeps native editor and draft open");
            check(session.editor.isEnabled(), "failed save re-enables editing");
            check(session.document.optString("text").equals(session.editor.getText().toString()),
                    "failed save preserves exact draft");
            dismissAlert(session);
            session.requestSave();
            check(saveCalls == 2, "save retry invokes callback again");
            NativeSongLyricsEditor.completeSave(sessionId, true, "");
        });
        waitForIdleSync();
        check(onUi(() -> NativeSongLyricsEditor.activeForTesting(sessionId) == null),
                "successful save dismisses native editor");
        check(closeCalls == 1, "successful save closes callback exactly once");
        check(onUi(() -> activity.getRequestedOrientation()) == originalOrientation,
                "editor restores previous requested orientation");

        JSONObject saved = new JSONObject(savedDocument);
        show(saved);
        ui(() -> {
            check(active().document.optString("text").equals(saved.optString("text")) &&
                    active().document.optJSONArray("sections").toString().equals(saved.optJSONArray("sections").toString()),
                    "reopening canonical saved document preserves IDs and text");
            active().editor.getText().append(" cambio");
            active().requestClose();
            check(active().dialog.isShowing(), "dirty close asks before discarding draft");
            dismissAlert(active());
            check(active().editor.getText().toString().endsWith(" cambio"), "keep editing retains unsaved changes");
        });
        int beforeCancel = closeCalls;
        dismiss();
        check(closeCalls == beforeCancel, "external cancellation dismisses silently without close callback");
        ui(() -> NativeSongLyricsEditor.completeSave(sessionId, true, ""));
        check(onUi(() -> NativeSongLyricsEditor.activeForTesting(sessionId) == null),
                "late save completion cannot recreate canceled editor");
    }

    private void screenshot(String name) throws Exception {
        waitForIdleSync();
        // Let only the native window/IME animation settle; no app state is inferred from delay.
        android.os.SystemClock.sleep(350);
        Bitmap bitmap;
        if (name.endsWith("-clean.png") || name.endsWith("-selection.png")) {
            bitmap = onUi(() -> {
                View view = active().dialog.getWindow().getDecorView();
                Bitmap captured = Bitmap.createBitmap(view.getWidth(), view.getHeight(), Bitmap.Config.ARGB_8888);
                view.draw(new Canvas(captured));
                return captured;
            });
        } else bitmap = getUiAutomation().takeScreenshot();
        check(bitmap != null && bitmap.getWidth() > 0, "native screen capture " + name);
        File target = new File(getTargetContext().getFilesDir(), name);
        try (FileOutputStream output = new FileOutputStream(target)) {
            check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, output), "write screen capture " + name);
        } finally { bitmap.recycle(); }
    }

    private void editingDiagnostic(NativeSongLyricsEditor.EditorSession session, String phase) {
        try {
            JSONArray states = new JSONArray();
            for (NativeSongLyricsEditor.HistoryEntry entry : session.history)
                states.put(new JSONObject().put("text", entry.text).put("json", entry.json));
            failures.put(new JSONObject().put("diagnostic", phase)
                    .put("nativeText", session.editor.getText().toString()).put("document", session.document)
                    .put("selectionStart", session.editor.getSelectionStart()).put("selectionEnd", session.editor.getSelectionEnd())
                    .put("historyIndex", session.historyIndex).put("history", states));
        } catch (Exception ignored) {}
    }

    private void assertCardGeometry(NativeSongLyricsEditor.EditorSession session, String id, String label) {
        JSONObject section = findSection(session.document, id);
        check(section != null, "section exists for layout " + label);
        android.text.Layout layout = session.editor.getLayout();
        check(layout != null, "native text layout exists " + label);
        RectF card = session.editor.cardRectForTesting(id);
        int first = layout.getLineForOffset(section.optInt("start"));
        int lastOffset = section.optInt("start") + section.optInt("length") - 1;
        int last = layout.getLineForOffset(Math.min(lastOffset, session.editor.length()));
        android.graphics.Paint.FontMetricsInt metrics = session.editor.getPaint().getFontMetricsInt();
        float firstGlyphTop = session.editor.getPaddingTop() + layout.getLineBaseline(first) + metrics.ascent;
        float lastGlyphBottom = session.editor.getPaddingTop() + layout.getLineBaseline(last) + metrics.descent;
        // Semantic geometry against real platform baselines, not a repeated card-layout formula.
        float headerBottom = card.top - session.dp(14) + session.headerHeight();
        check(!card.isEmpty() && card.height() >= lastGlyphBottom - firstGlyphTop + session.dp(20),
                "card has body height and padding " + label);
        check(firstGlyphTop >= headerBottom + session.dp(3),
                "real first glyph is below section header " + label);
        check(lastGlyphBottom <= card.bottom - session.dp(8),
                "all native glyph baselines fit inside card " + label);
        int end = section.optInt("start") + section.optInt("length");
        String text = session.editor.getText().toString();
        // The very next nonempty paragraph must not appear in this card's footer.
        if (end < text.length() && text.charAt(end) == '\n' && end + 1 < text.length() && text.charAt(end + 1) != '\n') {
            int nextLine = layout.getLineForOffset(end + 1);
            float nextGlyphTop = session.editor.getPaddingTop() + layout.getLineBaseline(nextLine) + metrics.ascent;
            check(nextGlyphTop >= card.bottom + session.dp(2),
                    "following unmarked paragraph stays outside previous card " + label);
        }
    }

    private void assertCaretInsideCard(NativeSongLyricsEditor.EditorSession session, String id, String label) {
        assertCardGeometry(session, id, label);
        RectF card = session.editor.cardRectForTesting(id);
        android.text.Layout layout = session.editor.getLayout();
        int line = layout.getLineForOffset(session.editor.getSelectionStart());
        android.graphics.Paint.FontMetricsInt metrics = session.editor.getPaint().getFontMetricsInt();
        float top = session.editor.getTotalPaddingTop() + layout.getLineBaseline(line) + metrics.ascent;
        float bottom = session.editor.getTotalPaddingTop() + layout.getLineBaseline(line) + metrics.descent;
        check(card != null && top >= card.top - 1 && bottom <= card.bottom + 1,
                "editable caret line stays in card " + label);
    }

    private void dismissAlert(NativeSongLyricsEditor.EditorSession session) {
        // Only the native editor's own alert; never interact with unrelated system UI.
        try {
            java.lang.reflect.Field field = session.getClass().getDeclaredField("activeAlert");
            field.setAccessible(true);
            android.app.AlertDialog alert = (android.app.AlertDialog) field.get(session);
            if (alert != null && alert.isShowing()) alert.dismiss();
        } catch (ReflectiveOperationException error) { throw new RuntimeException(error); }
    }

    private NativeSongLyricsEditor.EditorSession active() {
        NativeSongLyricsEditor.EditorSession result = NativeSongLyricsEditor.activeForTesting(sessionId);
        if (result == null) throw new AssertionError("Missing active native editor");
        return result;
    }

    private void show(JSONObject document) throws Exception {
        final JSONObject config = config();
        ++sessionId;
        ui(() -> NativeSongLyricsEditor.show(activity, sessionId, config.toString(), document.toString()));
        waitForIdleSync();
    }
    private void dismiss() { ui(() -> NativeSongLyricsEditor.dismiss(sessionId)); waitForIdleSync(); }

    private JSONObject transform(JSONObject document, JSONObject operation) throws Exception {
        String response = NativeSongLyricsEditor.nativeTransform(document.toString(), operation.toString());
        JSONObject result = new JSONObject(response);
        if (result.has("error")) throw new AssertionError("Native transform rejected " + operation.optString("action") + ": " + result.optString("error"));
        return result;
    }
    private static JSONObject document(String text, JSONArray sections) throws Exception {
        return new JSONObject().put("schemaVersion", 1).put("offsetEncoding", "utf16").put("text", text).put("sections", sections);
    }
    private static JSONObject section(String id, String kind, int start, int length) throws Exception {
        return new JSONObject().put("id", id).put("kind", kind).put("start", start).put("length", length);
    }
    private static JSONObject operation(String action, int start, int length) throws Exception {
        return new JSONObject().put("action", action).put("start", start).put("length", length);
    }
    private static JSONObject findSection(JSONObject document, String id) {
        JSONArray sections = document.optJSONArray("sections");
        for (int i = 0; sections != null && i < sections.length(); ++i) {
            JSONObject section = sections.optJSONObject(i);
            if (section != null && id.equals(section.optString("id"))) return section;
        }
        return null;
    }
    private JSONObject config() throws Exception {
        JSONObject colors = new JSONObject().put("background", "#1E1E1E").put("text", "#EBEBEB")
                .put("secondary", "#B8B8B8").put("accent", "#FFB347").put("pageBackground", "#2A2A2A")
                .put("pageBackgroundTop", "#2A2A2A").put("pageBackgroundBottom", "#343434")
                .put("cardBackground", "#343434").put("cardBorder", "#444444").put("sectionHeaderText", "#27302B");
        JSONArray kinds = new JSONArray();
        String[] ids = {"intro", "verse", "prechorus", "chorus", "bridge", "instrumental", "outro", "ending"};
        String[] titles = {"Intro", "Verso", "Pre-coro", "Coro", "Puente", "Instrumental", "Outro", "Final"};
        String[] codes = {"IN", "V", "PC", "C", "P", "I", "O", "F"};
        String[] palette = {"#B4F89C", "#A9E6F5", "#EEB1EC", "#FFC5AB", "#B6C8FB", "#CBE8A6", "#F2D39D", "#E8B9CC"};
        for (int i = 0; i < ids.length; ++i) kinds.put(new JSONObject().put("id", ids[i]).put("title", titles[i]).put("code", codes[i]).put("color", palette[i]));
        return new JSONObject().put("title", "Luz de esperanza").put("subtitle", "PlayWorship · Demo · 96 BPM")
                .put("heading", "Letra").put("placeholder", "Pega aquí la letra de la canción o empieza a escribir.\n\nSelecciona un fragmento y elige Sección.")
                .put("saveTitle", "Listo").put("closeTitle", "Cerrar").put("sectionActionTitle", "Sección")
                .put("clearSectionTitle", "Quitar sección").put("errorTitle", "No se pudo guardar")
                .put("savingTitle", "Guardando…").put("discardTitle", "¿Descartar los cambios?")
                .put("discardMessage", "La letra conserva los últimos cambios que guardaste.")
                .put("discardConfirmTitle", "Descartar").put("keepEditingTitle", "Seguir editando")
                .put("colors", colors).put("sectionKinds", kinds).put("sectionLabels", new JSONArray());
    }
    private int countEditTexts(View view) {
        int count = view instanceof EditText ? 1 : 0;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); ++i) count += countEditTexts(group.getChildAt(i));
        }
        return count;
    }
    private void check(boolean condition, String label) {
        checks.put(label);
        if (!condition) {
            failures.put(label);
            throw new AssertionError(label);
        }
    }
    private void failure(String label, Throwable error) {
        JSONObject value = new JSONObject();
        try { value.put("label", label).put("type", error.getClass().getName()).put("message", String.valueOf(error.getMessage())); }
        catch (Exception ignored) {}
        failures.put(value);
    }
    private void ui(Runnable operation) {
        AtomicReference<Throwable> failure = new AtomicReference<>();
        runOnMainSync(() -> { try { operation.run(); } catch (Throwable error) { failure.set(error); } });
        Throwable error = failure.get();
        if (error != null) throw new RuntimeException(error);
    }
    private interface Value<T> { T get(); }
    private <T> T onUi(Value<T> operation) {
        AtomicReference<T> value = new AtomicReference<>();
        ui(() -> value.set(operation.get()));
        return value.get();
    }
}
