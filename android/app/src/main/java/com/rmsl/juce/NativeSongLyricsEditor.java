package com.rmsl.juce;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputFilter;
import android.text.Layout;
import android.text.Spanned;
import android.text.TextWatcher;
import android.text.method.KeyListener;
import android.text.style.BackgroundColorSpan;
import android.text.style.LineHeightSpan;
import android.util.TypedValue;
import android.view.ActionMode;
import android.view.ContextThemeWrapper;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputConnectionWrapper;
import android.view.inputmethod.EditorInfo;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;

/** Native Android editor. The EditText owns text, IME, selection and its undo stack;
 * cards and section badges are presentation only, never hidden text characters. */
public final class NativeSongLyricsEditor {
    private static final Handler ui = new Handler(Looper.getMainLooper());
    private static final Map<Long, EditorSession> sessions = new HashMap<>();
    static TestObserver testObserver;
    interface TestObserver {
        void onSave(long sessionId, String documentJson);
        void onClose(long sessionId);
    }

    static native String nativeTransform(String documentJson, String operationJson);
    private static native boolean nativeIsActive(long sessionId);
    private static native void nativeSave(long sessionId, String documentJson);
    private static native void nativeClose(long sessionId);

    private NativeSongLyricsEditor() {}

    public static void show(Activity activity, long sessionId, String configJson, String documentJson) {
        activity.runOnUiThread(() -> {
            if (testObserver == null && !nativeIsActive(sessionId)) return;
            if (activity.isFinishing() || activity.isDestroyed()) {
                notifyClose(sessionId);
                return;
            }
            EditorSession previous = sessions.get(sessionId);
            if (previous != null) previous.finish(false);
            try {
                EditorSession session = new EditorSession(activity, sessionId,
                        new JSONObject(configJson), new JSONObject(documentJson));
                sessions.put(sessionId, session);
                session.show();
            } catch (Exception failure) {
                EditorSession failed = sessions.remove(sessionId);
                if (failed != null) failed.finish(false);
                notifyClose(sessionId);
            }
        });
    }

    public static void dismiss(long sessionId) {
        ui.post(() -> {
            EditorSession session = sessions.get(sessionId);
            if (session != null) session.finish(false);
        });
    }

    public static void dismissForActivity(Activity activity) {
        activity.runOnUiThread(() -> {
            for (EditorSession session : new ArrayList<>(sessions.values()))
                if (session.activity == activity) session.finish(true);
        });
    }

    public static void completeSave(long sessionId, boolean success, String error) {
        ui.post(() -> {
            EditorSession session = sessions.get(sessionId);
            if (session != null) session.completeSave(success, error);
        });
    }

    static EditorSession activeForTesting(long sessionId) { return sessions.get(sessionId); }

    private static void notifyClose(long sessionId) {
        if (testObserver != null) testObserver.onClose(sessionId);
        else nativeClose(sessionId);
    }

    static final class EditorSession {
        final Activity activity;
        final long sessionId;
        final JSONObject config;
        JSONObject document;
        JSONArray labels = new JSONArray();
        final String initialDocument;
        final Dialog dialog;
        AlertDialog activeAlert;
        final PageLayout root;
        final LinearLayout column;
        final LinearLayout sectionRow;
        final HorizontalScrollView sectionScroll;
        final CardEditText editor;
        final TextView saveButton;
        final View closeButton;
        final int previousOrientation;
        final boolean changesOrientation;
        final int foreground, secondary, accent, card, cardBorder, headerText;
        final ArrayList<HistoryEntry> history = new ArrayList<>();
        int historyIndex;
        boolean ending, busy, styling, applyingText, historyMoving, presentationPending;
        int historyDirection;
        String activeSectionId = "";
        OnBackInvokedCallback backCallback;
        String previousText = "";
        int replacementStart, replacementLength;
        final Runnable refreshAfterComposition = this::refreshPresentation;

        EditorSession(Activity activity, long id, JSONObject config, JSONObject sourceDocument) throws JSONException {
            this.activity = activity;
            this.sessionId = id;
            this.config = config;
            this.document = sourceDocument;
            initialDocument = sourceDocument.toString();
            JSONObject colors = config.optJSONObject("colors");
            foreground = color(colors, "text", Color.WHITE);
            secondary = color(colors, "secondary", 0xffaaaaaa);
            accent = color(colors, "accent", 0xff35cf83);
            card = color(colors, "cardBackground", 0xff303030);
            cardBorder = color(colors, "cardBorder", 0xff404040);
            headerText = color(colors, "sectionHeaderText", 0xff27302b);
            int pageTop = color(colors, "pageBackgroundTop", 0xff222222);
            int pageBottom = color(colors, "pageBackgroundBottom", 0xff303030);
            boolean dark = luminance(pageTop) < 0.5;
            Context context = new ContextThemeWrapper(activity, dark
                    ? android.R.style.Theme_Material_NoActionBar
                    : android.R.style.Theme_Material_Light_NoActionBar);
            previousOrientation = activity.getRequestedOrientation();
            changesOrientation = activity.getResources().getConfiguration().smallestScreenWidthDp < 600;
            dialog = new Dialog(context);
            dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
            dialog.setCancelable(false);
            dialog.setOnKeyListener((which, key, event) -> {
                if (key == KeyEvent.KEYCODE_BACK && event.getAction() == KeyEvent.ACTION_UP) {
                    // The first Back belongs to the IME when it is visible.
                    handleBack();
                    return true;
                }
                return false;
            });
            root = new PageLayout(context, pageTop, pageBottom, accent);
            root.setTag("nativeLyricsPage");
            root.setFocusableInTouchMode(true);
            column = new LinearLayout(context);
            column.setOrientation(LinearLayout.VERTICAL);
            root.addView(column, new FrameLayout.LayoutParams(-1, -1));
            LinearLayout toolbar = new LinearLayout(context);
            toolbar.setGravity(Gravity.CENTER_VERTICAL);
            toolbar.setPadding(dp(8), 0, dp(8), 0);
            column.addView(toolbar, new LinearLayout.LayoutParams(-1, Math.max(dp(52), sp(18) + dp(28))));
            ArrowButton back = new ArrowButton(context, foreground);
            back.setTag("nativeLyricsClose");
            back.setContentDescription(label("closeTitle"));
            back.setOnClickListener(v -> requestClose());
            closeButton = back;
            toolbar.addView(back, new LinearLayout.LayoutParams(navigationHeight(), navigationHeight()));
            TextView heading = text(context, label("heading"), 18, foreground, true);
            heading.setGravity(Gravity.CENTER);
            toolbar.addView(heading, new LinearLayout.LayoutParams(0, -1, 1));
            saveButton = text(context, label("saveTitle"), 16, accent, true);
            saveButton.setTag("nativeLyricsSave");
            saveButton.setGravity(Gravity.CENTER);
            saveButton.setMinWidth(dp(64));
            saveButton.setPadding(dp(8), 0, dp(8), 0);
            saveButton.setOnClickListener(v -> requestSave());
            saveButton.setBackground(selectable(context));
            toolbar.addView(saveButton, new LinearLayout.LayoutParams(-2, dp(48)));
            TextView song = text(context, config.optString("title"), 24, foreground, true);
            song.setTag("nativeLyricsSongTitle");
            song.setMaxLines(2);
            LinearLayout.LayoutParams songParams = new LinearLayout.LayoutParams(-1, -2);
            songParams.setMargins(dp(16), dp(12), dp(16), 0);
            column.addView(song, songParams);
            TextView metadata = text(context, config.optString("subtitle"), 13, secondary, false);
            metadata.setTag("nativeLyricsMetadata");
            LinearLayout.LayoutParams metadataParams = new LinearLayout.LayoutParams(-1, -2);
            metadataParams.setMargins(dp(16), dp(4), dp(16), dp(8));
            column.addView(metadata, metadataParams);
            if (metadata.getText().length() == 0) metadata.setVisibility(View.GONE);
            sectionScroll = new HorizontalScrollView(context);
            sectionScroll.setTag("nativeLyricsSections");
            sectionScroll.setHorizontalScrollBarEnabled(false);
            sectionScroll.setClipToPadding(false);
            sectionScroll.setPadding(dp(12), 0, dp(12), 0);
            sectionRow = new LinearLayout(context);
            sectionRow.setGravity(Gravity.CENTER_VERTICAL);
            sectionScroll.addView(sectionRow, new FrameLayout.LayoutParams(-2, -1));
            column.addView(sectionScroll, new LinearLayout.LayoutParams(-1, navigationHeight()));
            editor = new CardEditText(context, this);
            editor.setTag("nativeLyricsText");
            editor.setContentDescription(label("heading"));
            editor.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
            editor.setTextColor(foreground);
            editor.setHintTextColor(secondary);
            editor.setHint(label("placeholder"));
            editor.setGravity(Gravity.TOP | Gravity.START);
            editor.setSingleLine(false);
            editor.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                    | android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE
                    | android.text.InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
            editor.setImeOptions(android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI);
            editor.setBackgroundColor(Color.TRANSPARENT);
            editor.setPadding(dp(24), dp(24), dp(24), dp(32));
            editor.setIncludeFontPadding(false);
            editor.setLineSpacing(dp(6), 1);
            editor.setHorizontallyScrolling(false);
            editor.setSelectAllOnFocus(false);
            editor.setTextIsSelectable(true);
            editor.setFilters(new InputFilter[]{new UnicodeLengthFilter(262144)});
            editor.setHighlightColor((accent & 0xffffff) | 0x55000000);
            editor.setTextCursorDrawable(new LyricsCursor(editor, accent));
            // Public Android API preserves the platform handle shape and touch
            // behavior while inheriting the app's current accent, like UIKit tint.
            if (editor.getTextSelectHandle() != null)
                editor.setTextSelectHandle(tinted(editor.getTextSelectHandle(), accent));
            if (editor.getTextSelectHandleLeft() != null)
                editor.setTextSelectHandleLeft(tinted(editor.getTextSelectHandleLeft(), accent));
            if (editor.getTextSelectHandleRight() != null)
                editor.setTextSelectHandleRight(tinted(editor.getTextSelectHandleRight(), accent));
            editor.setText(document.optString("text"));
            previousText = editor.getText().toString();
            editor.setCustomSelectionActionModeCallback(new ActionMode.Callback() {
                public boolean onCreateActionMode(ActionMode mode, Menu menu) {
                    addSectionAction(menu); return true;
                }
                public boolean onPrepareActionMode(ActionMode mode, Menu menu) {
                    addSectionAction(menu); return false;
                }
                public boolean onActionItemClicked(ActionMode mode, MenuItem item) {
                    if (item.getItemId() != SECTION_ACTION_ID) return false;
                    int start = Math.min(editor.getSelectionStart(), editor.getSelectionEnd());
                    int end = Math.max(editor.getSelectionStart(), editor.getSelectionEnd());
                    if (!busy && start >= 0 && end > start) showSectionPicker(start, end);
                    return true;
                }
                public void onDestroyActionMode(ActionMode mode) {}
            });
            editor.addTextChangedListener(new TextWatcher() {
                public void beforeTextChanged(CharSequence value, int start, int count, int after) {
                    if (styling || applyingText) return;
                    previousText = value.toString();
                    replacementStart = start;
                    replacementLength = count;
                }
                public void onTextChanged(CharSequence value, int start, int before, int count) {
                    if (styling || applyingText || ending) return;
                    String next = value.toString();
                    if (next.equals(document.optString("text"))) return;
                    if (historyMoving && restoreHistoryForText(next)) return;
                    try {
                        JSONObject op = new JSONObject().put("action", "replace")
                                .put("start", start).put("length", before)
                                .put("text", next.substring(start, start + count));
                        JSONObject result = transform(op);
                        if (result != null) {
                            acceptResult(result);
                            recordHistory();
                        } else {
                            // Native validation rejects an invalid UTF-16 replacement.
                            // Defer restoration until the current IME transaction ends.
                            final String valid = document.optString("text");
                            ui.post(() -> restoreRejectedText(valid));
                        }
                    } catch (JSONException ignored) {}
                }
                public void afterTextChanged(Editable value) {
                    if (styling || applyingText || ending) return;
                    schedulePresentation();
                }
            });
            column.addView(editor, new LinearLayout.LayoutParams(-1, 0, 1));
            labels = config.optJSONArray("sectionLabels");
            if (labels == null) labels = new JSONArray();
            JSONObject initial = transform(new JSONObject().put("action", "labels"));
            if (initial != null) acceptResult(initial);
            history.add(new HistoryEntry(this.document));
            root.requestFocus();
        }

        void show() {
            if (changesOrientation) activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
            dialog.setContentView(root);
            dialog.show();
            Window window = dialog.getWindow();
            if (window == null) { finish(true); return; }
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.setLayout(-1, -1);
            window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE
                    | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
            window.clearFlags(WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM);
            boolean dark = luminance(root.topColor) < .5;
            window.setStatusBarColor(root.topColor);
            window.setNavigationBarColor(root.bottomColor);
            int flags = dark ? 0 : View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
            window.getDecorView().setSystemUiVisibility(flags);
            if (Build.VERSION.SDK_INT >= 30) {
                window.setDecorFitsSystemWindows(false);
                root.setOnApplyWindowInsetsListener((view, insets) -> {
                    android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars()
                            | WindowInsets.Type.displayCutout());
                    android.graphics.Insets keyboard = insets.getInsets(WindowInsets.Type.ime());
                    root.setPadding(bars.left, bars.top, bars.right, Math.max(bars.bottom, keyboard.bottom));
                    return insets;
                });
                root.requestApplyInsets();
            } else root.setFitsSystemWindows(true);
            if (Build.VERSION.SDK_INT >= 33) {
                backCallback = this::handleBack;
                dialog.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                        OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
            }
            refreshPresentation();
        }

        void handleBack() {
            WindowInsets insets = root.getRootWindowInsets();
            if (Build.VERSION.SDK_INT >= 30 && insets != null && insets.isVisible(WindowInsets.Type.ime())) {
                InputMethodManager ime = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
                if (ime != null) ime.hideSoftInputFromWindow(editor.getWindowToken(), 0);
                return;
            }
            requestClose();
        }

        private void addSectionAction(Menu menu) {
            if (menu.findItem(SECTION_ACTION_ID) == null)
                menu.add(Menu.NONE, SECTION_ACTION_ID, 1, label("sectionActionTitle"))
                        .setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM);
        }

        void showSectionPicker(int start, int end) {
            JSONArray kinds = config.optJSONArray("sectionKinds");
            if (kinds == null || ending || busy) return;
            String[] choices = new String[kinds.length() + 1];
            for (int i = 0; i < kinds.length(); ++i)
                choices[i] = kinds.optJSONObject(i).optString("title");
            choices[kinds.length()] = label("clearSectionTitle");
            final JSONArray options = kinds;
            final String revision = document.toString();
            activeAlert = new AlertDialog.Builder(dialog.getContext())
                    .setTitle(label("sectionActionTitle"))
                    .setItems(choices, (which, item) -> {
                        if (!revision.equals(document.toString())) return;
                        if (item < options.length())
                            applySection(options.optJSONObject(item).optString("id"), start, end);
                        else applySection("", start, end);
                    }).create();
            activeAlert.setOnDismissListener(which -> activeAlert = null);
            activeAlert.show();
            tintAlertButtons(activeAlert);
        }

        void applySection(String kind, int start, int end) {
            if (busy || ending || start < 0 || end <= start || end > editor.length()) return;
            try {
                JSONObject operation = new JSONObject().put("action", kind.isEmpty() ? "clear" : "section")
                        .put("start", start).put("length", end - start).put("kind", kind);
                JSONObject result = transform(operation);
                if (result != null) {
                    acceptResult(result);
                    // Manual reclassification is a new metadata baseline. An
                    // older native text Undo must not resurrect previous kinds.
                    history.clear();
                    history.add(new HistoryEntry(document));
                    historyIndex = 0;
                    refreshPresentation();
                }
            } catch (JSONException ignored) {}
        }

        JSONObject transform(JSONObject operation) {
            try {
                String response = nativeTransform(document.toString(), operation.toString());
                if (response == null) return null;
                JSONObject result = new JSONObject(response);
                return result.has("error") || !result.has("document") ? null : result;
            } catch (JSONException ignored) { return null; }
        }

        void acceptResult(JSONObject result) {
            JSONObject updated = result.optJSONObject("document");
            if (updated != null) document = updated;
            JSONArray updatedLabels = result.optJSONArray("labels");
            if (updatedLabels != null) labels = updatedLabels;
        }

        void schedulePresentation() {
            editor.removeCallbacks(refreshAfterComposition);
            presentationPending = true;
            if (BaseInputConnection.getComposingSpanStart(editor.getText()) >= 0) return;
            refreshPresentation();
        }

        void refreshPresentation() {
            if (ending || styling) return;
            if (BaseInputConnection.getComposingSpanStart(editor.getText()) >= 0) {
                presentationPending = true;
                return;
            }
            presentationPending = false;
            styling = true;
            editor.beginBatchEdit();
            try {
                editor.updateSections(document.optJSONArray("sections"), labels);
                refreshNavigation();
            } finally {
                editor.endBatchEdit();
                styling = false;
            }
            editor.invalidate();
        }

        void refreshNavigation() {
            LinkedHashMap<String, SectionPresentation> sections = editor.sections;
            boolean rebuild = sectionRow.getChildCount() != sections.size();
            if (!rebuild) {
                int index = 0;
                for (SectionPresentation section : sections.values()) {
                    SectionButton child = (SectionButton) sectionRow.getChildAt(index++);
                    if (!child.section.id.equals(section.id) || !child.section.code.equals(section.code)
                            || !child.section.label.equals(section.label)) { rebuild = true; break; }
                }
            }
            if (rebuild) {
                sectionRow.removeAllViews();
                for (SectionPresentation section : sections.values()) {
                    SectionButton button = new SectionButton(dialog.getContext(), this, section);
                    button.setOnClickListener(v -> navigateToSection(section.id));
                    sectionRow.addView(button, new LinearLayout.LayoutParams(navigationHeight(), navigationHeight()));
                }
            }
            sectionScroll.setVisibility(sections.isEmpty() ? View.GONE : View.VISIBLE);
            for (int i = 0; i < sectionRow.getChildCount(); ++i) {
                SectionButton child = (SectionButton) sectionRow.getChildAt(i);
                boolean selected = child.section.id.equals(activeSectionId);
                if (child.isSelected() != selected) child.setSelected(selected);
            }
        }

        void navigateToSection(String id) {
            if (busy || ending) return;
            SectionPresentation section = editor.sections.get(id);
            if (section == null) return;
            activeSectionId = id;
            // setSelection does not request focus or show the keyboard.
            editor.setSelection(Math.min(section.start, editor.length()));
            refreshNavigation();
            editor.post(() -> {
                if (ending || editor.getLayout() == null) return;
                RectF card = editor.cardRectForTesting(id);
                int y = Math.max(0, Math.round(card.top - dp(20)));
                editor.scrollTo(editor.getScrollX(), Math.min(y,
                        Math.max(0, editor.getLayout().getHeight() + editor.getPaddingTop()
                                + editor.getPaddingBottom() - editor.getHeight())));
            });
        }

        void requestSave() {
            if (busy || ending) return;
            busy = true;
            setBusy(true);
            if (testObserver != null) testObserver.onSave(sessionId, document.toString());
            else nativeSave(sessionId, document.toString());
        }

        void completeSave(boolean success, String error) {
            if (ending || !busy) return;
            if (success) { finish(true); return; }
            busy = false;
            setBusy(false);
            activeAlert = new AlertDialog.Builder(dialog.getContext())
                    .setTitle(label("errorTitle"))
                    .setMessage(error == null || error.isEmpty() ? label("errorTitle") : error)
                    .setPositiveButton(android.R.string.ok, null).create();
            activeAlert.setOnDismissListener(which -> activeAlert = null);
            activeAlert.show();
            tintAlertButtons(activeAlert);
        }

        void tintAlertButtons(AlertDialog alert) {
            for (int id : new int[]{AlertDialog.BUTTON_POSITIVE, AlertDialog.BUTTON_NEGATIVE, AlertDialog.BUTTON_NEUTRAL})
                if (alert.getButton(id) != null) alert.getButton(id).setTextColor(accent);
        }

        void setBusy(boolean value) {
            saveButton.setEnabled(!value);
            closeButton.setEnabled(!value);
            saveButton.setText(label(value ? "savingTitle" : "saveTitle"));
            editor.setEnabled(!value);
            sectionRow.setEnabled(!value);
            for (int i = 0; i < sectionRow.getChildCount(); ++i) sectionRow.getChildAt(i).setEnabled(!value);
        }

        void requestClose() {
            if (ending || busy) return;
            if (document.toString().equals(initialDocument)) { finish(true); return; }
            if (activeAlert != null) return;
            activeAlert = new AlertDialog.Builder(dialog.getContext())
                    .setTitle(label("discardTitle"))
                    .setMessage(label("discardMessage"))
                    .setNegativeButton(label("keepEditingTitle"), null)
                    .setPositiveButton(label("discardConfirmTitle"), (which, item) -> finish(true))
                    .create();
            activeAlert.setOnDismissListener(which -> activeAlert = null);
            activeAlert.show();
            tintAlertButtons(activeAlert);
        }

        void finish(boolean notify) {
            if (ending) return;
            ending = true;
            sessions.remove(sessionId);
            editor.removeCallbacks(refreshAfterComposition);
            if (activeAlert != null) { activeAlert.dismiss(); activeAlert = null; }
            InputMethodManager ime = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            if (ime != null) ime.hideSoftInputFromWindow(editor.getWindowToken(), 0);
            if (Build.VERSION.SDK_INT >= 33 && backCallback != null) {
                dialog.getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
                backCallback = null;
            }
            dialog.dismiss();
            if (changesOrientation && !activity.isDestroyed()) activity.setRequestedOrientation(previousOrientation);
            if (notify) notifyClose(sessionId);
        }

        void restoreRejectedText(String text) {
            if (ending || text.equals(editor.getText().toString())) return;
            applyingText = true;
            int cursor = Math.min(editor.getSelectionStart(), text.length());
            editor.getText().replace(0, editor.length(), text);
            editor.setSelection(Math.max(0, cursor));
            applyingText = false;
            refreshPresentation();
        }

        void recordHistory() {
            if (historyMoving) return;
            String json = document.toString();
            if (!history.isEmpty() && history.get(historyIndex).json.equals(json)) return;
            while (history.size() > historyIndex + 1) history.remove(history.size() - 1);
            history.add(new HistoryEntry(document));
            historyIndex = history.size() - 1;
            long retainedBytes = 0;
            for (HistoryEntry entry : history) retainedBytes += entry.json.length() * 2L;
            while (history.size() > 1 && (history.size() > 256 || retainedBytes > 4L * 1024 * 1024)) {
                retainedBytes -= history.get(0).json.length() * 2L;
                history.remove(0); --historyIndex;
            }
        }

        boolean restoreHistoryForText(String text) {
            for (int i = historyIndex + historyDirection; i >= 0 && i < history.size(); i += historyDirection) {
                HistoryEntry entry = history.get(i);
                if (!entry.text.equals(text)) continue;
                try {
                    document = new JSONObject(entry.json);
                    historyIndex = i;
                    JSONObject result = transform(new JSONObject().put("action", "labels"));
                    if (result != null) acceptResult(result);
                    return true;
                } catch (JSONException ignored) { return false; }
            }
            return false;
        }

        String label(String key) { return config.optString(key); }
        int dp(float value) { return Math.round(value * activity.getResources().getDisplayMetrics().density); }
        int sp(float value) { return Math.round(value * activity.getResources().getDisplayMetrics().scaledDensity); }
        int navigationHeight() { return Math.max(dp(48), sp(13) + dp(30)); }
        int headerHeight() { return Math.max(dp(32), sp(13) + dp(16)); }
        int headerCircle() { return Math.max(dp(24), sp(12) + dp(9)); }
        int headingGap() { return headerHeight() + dp(24); }
    }

    private static final int SECTION_ACTION_ID = 0x50574c01;

    static final class HistoryEntry {
        final String text, json;
        HistoryEntry(JSONObject value) { text = value.optString("text"); json = value.toString(); }
    }

    static final class SectionPresentation {
        final String id, code, label;
        final int start, end, color;
        final boolean card;
        SectionSpacing spacing;
        ParagraphGap footerGap;
        BackgroundColorSpan inline;
        SectionPresentation(JSONObject section, JSONObject presentation, String text) {
            id = section.optString("id");
            start = section.optInt("start"); end = start + section.optInt("length");
            color = parseColor(presentation == null ? "" : presentation.optString("color"), 0xffa9e6f5);
            code = presentation == null ? "" : presentation.optString("code");
            label = presentation == null ? "" : presentation.optString("label");
            boolean valid = start >= 0 && end > start && end <= text.length();
            boolean startsParagraph = valid && (start == 0 || (start > 0 && isNewline(text.charAt(start - 1))
                    && !(text.charAt(start - 1) == '\r' && start < text.length() && text.charAt(start) == '\n')));
            boolean endsParagraph = valid && (end == text.length() || (end < text.length() && isNewline(text.charAt(end)))
                    || (end > 0 && isNewline(text.charAt(end - 1))));
            card = valid && (presentation != null && presentation.has("completeBlock")
                    ? presentation.optBoolean("completeBlock")
                    : startsParagraph && endsParagraph && !(end > 0 && end < text.length()
                    && text.charAt(end - 1) == '\r' && text.charAt(end) == '\n'));
        }
    }

    /** All decoration is drawn before native selection/text. The only layout span
     * reserves heading space in ascent; descent stays native for selection handles. */
    static final class CardEditText extends EditText {
        final EditorSession session;
        final LinkedHashMap<String, SectionPresentation> sections = new LinkedHashMap<>();
        final HashSet<Integer> sectionStarts = new HashSet<>();
        final HashSet<Integer> cardStarts = new HashSet<>();
        final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        final ArrayList<RectF> headingBands = new ArrayList<>();

        CardEditText(Context context, EditorSession session) { super(context); this.session = session; }

        void updateSections(JSONArray values, JSONArray labels) {
            Editable text = getText();
            String content = text.toString();
            LinkedHashMap<String, SectionPresentation> desired = new LinkedHashMap<>();
            Map<String, JSONObject> byID = new HashMap<>();
            if (labels != null) for (int i = 0; i < labels.length(); ++i) {
                JSONObject label = labels.optJSONObject(i);
                if (label != null) byID.put(label.optString("id"), label);
            }
            if (values != null) for (int i = 0; i < values.length(); ++i) {
                JSONObject value = values.optJSONObject(i);
                if (value == null) continue;
                JSONObject presentation = byID.get(value.optString("id"));
                SectionPresentation section = new SectionPresentation(value, presentation, content);
                if (section.start < 0 || section.end > text.length() || section.end <= section.start) continue;
                desired.put(section.id, section);
            }
            for (SectionPresentation previous : sections.values()) {
                SectionPresentation replacement = desired.get(previous.id);
                if (replacement != null && previous.card == replacement.card && previous.color == replacement.color) {
                    replacement.spacing = previous.spacing; replacement.footerGap = previous.footerGap;
                    replacement.inline = previous.inline;
                } else {
                    if (previous.spacing != null) text.removeSpan(previous.spacing);
                    if (previous.footerGap != null) text.removeSpan(previous.footerGap);
                    if (previous.inline != null) text.removeSpan(previous.inline);
                }
            }
            sections.clear(); sections.putAll(desired);
            sectionStarts.clear(); cardStarts.clear();
            for (SectionPresentation section : sections.values()) {
                sectionStarts.add(section.start);
                if (section.card) cardStarts.add(section.start);
            }
            for (SectionSpacing span : text.getSpans(0, text.length(), SectionSpacing.class)) {
                SectionPresentation owner = sections.get(span.id);
                if (owner == null || !owner.card || owner.spacing != span) text.removeSpan(span);
            }
            for (ParagraphGap span : text.getSpans(0, text.length(), ParagraphGap.class)) {
                SectionPresentation owner = sections.get(span.id);
                if (owner == null || !owner.card || owner.footerGap != span) text.removeSpan(span);
            }
            for (SectionPresentation section : sections.values()) {
                if (section.card) {
                    if (section.spacing == null) section.spacing = new SectionSpacing(this, section.id);
                    int spanEnd = continuationEnd(section, content);
                    if (text.getSpanStart(section.spacing) != section.start || text.getSpanEnd(section.spacing) != spanEnd)
                        text.setSpan(section.spacing, section.start, spanEnd, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);
                    int following = followingParagraphStart(section, content);
                    boolean needsGap = following >= 0 && following < content.length()
                            && !isNewline(content.charAt(following));
                    if (needsGap && cardStarts.contains(following)) needsGap = false;
                    if (needsGap) {
                        if (section.footerGap == null) section.footerGap = new ParagraphGap(this, section.id);
                        section.footerGap.start = following;
                        if (text.getSpanStart(section.footerGap) != following || text.getSpanEnd(section.footerGap) != following + 1)
                            text.setSpan(section.footerGap, following, following + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
                    } else if (section.footerGap != null) {
                        text.removeSpan(section.footerGap);
                        section.footerGap = null;
                    }
                } else {
                    if (section.inline == null) section.inline = new BackgroundColorSpan((section.color & 0xffffff) | 0x35000000);
                    if (text.getSpanStart(section.inline) != section.start || text.getSpanEnd(section.inline) != section.end)
                        text.setSpan(section.inline, section.start, section.end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
                }
            }
        }

        int followingParagraphStart(SectionPresentation section, String text) {
            int end = section.end;
            if (end >= text.length()) return -1;
            if (end > 0 && isNewline(text.charAt(end - 1))) {
                boolean ownsEmpty = isNewline(text.charAt(end));
                if (ownsEmpty && sectionStarts.contains(end)) ownsEmpty = false;
                if (!ownsEmpty) return end;
            }
            if (isNewline(text.charAt(end))) {
                if (text.charAt(end) == '\r' && end + 1 < text.length() && text.charAt(end + 1) == '\n') return end + 2;
                return end + 1;
            }
            return end;
        }

        int continuationEnd(SectionPresentation section, String text) {
            int end = section.end;
            if (end > section.start && isNewline(text.charAt(end - 1)) && end < text.length()
                    && isNewline(text.charAt(end))) {
                if (sectionStarts.contains(end)) return end;
                return end + 1;
            }
            return end;
        }

        int lastLine(SectionPresentation section, Layout layout) {
            CharSequence text = getText();
            // IME may shorten the marked text before decoration is reapplied.
            // Never use cached presentation ranges to index the live Editable.
            int length = Math.min(text.length(), layout.getText().length());
            int start = Math.max(0, Math.min(section.start, length));
            int end = Math.max(start, Math.min(section.end, length));
            boolean ownsEmpty = end > start && isNewline(text.charAt(end - 1))
                    && (end == length || isNewline(text.charAt(end)));
            if (ownsEmpty && sectionStarts.contains(end)) ownsEmpty = false;
            return layout.getLineForOffset(Math.max(start, ownsEmpty ? end : end - 1));
        }

        RectF cardRectForTesting(String id) {
            Layout layout = getLayout();
            SectionPresentation section = sections.get(id);
            if (layout == null || section == null) return new RectF();
            int length = Math.min(length(), layout.getText().length());
            if (length == 0 || section.start >= length) return new RectF();
            int start = Math.max(0, Math.min(section.start, length));
            int first = layout.getLineForOffset(start), last = lastLine(section, layout);
            Paint.FontMetricsInt metrics = getPaint().getFontMetricsInt();
            float top = layout.getLineTop(first) + session.headingGap() - session.headerHeight() - dp(4);
            float bottom = layout.getLineBaseline(last) + metrics.descent + dp(16);
            return new RectF(dp(12), getPaddingTop() + top,
                    getWidth() - dp(12), getPaddingTop() + bottom);
        }

        @Override protected void onDraw(Canvas canvas) {
            Layout layout = getLayout();
            headingBands.clear();
            if (layout != null) {
                int original = canvas.save();
                // View's render pass already translated the canvas by -scroll.
                // Keep decoration coordinates in the same document space as TextView.
                for (SectionPresentation section : sections.values()) {
                    if (!section.card) continue;
                    RectF card = cardRectForTesting(section.id);
                    if (card.isEmpty() || card.bottom < getScrollY() || card.top > getScrollY() + getHeight()) continue;
                    paint.setColor(session.card); paint.setStyle(Paint.Style.FILL);
                    canvas.drawRoundRect(card, dp(16), dp(16), paint);
                    paint.setColor(session.cardBorder); paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(dp(.5f));
                    canvas.drawRoundRect(card, dp(16), dp(16), paint);
                    paint.setStyle(Paint.Style.FILL);
                    drawHeader(canvas, section, card.top - dp(14));
                    int first = layout.getLineForOffset(Math.max(0,
                            Math.min(section.start, Math.min(length(), layout.getText().length()))));
                    float textTop = getPaddingTop() + layout.getLineBaseline(first) + getPaint().getFontMetricsInt().ascent;
                    headingBands.add(new RectF(0, getPaddingTop() + layout.getLineTop(first), getWidth(), textTop));
                }
                canvas.restoreToCount(original);
            }
            int clipped = canvas.save();
            // Reserve decorative bands, keeping native selection on real text rows.
            for (RectF band : headingBands)
                canvas.clipOutRect(band.left, band.top, band.right, band.bottom);
            super.onDraw(canvas);
            canvas.restoreToCount(clipped);
        }

        void drawHeader(Canvas canvas, SectionPresentation section, float top) {
            float textSize = sp(13), circle = session.headerCircle(), height = session.headerHeight(), left = dp(24);
            paint.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
            paint.setTextSize(textSize);
            float width = circle + dp(20) + paint.measureText(section.label);
            paint.setColor(section.color);
            canvas.drawRoundRect(new RectF(left, top, left + width, top + height), height / 2, height / 2, paint);
            paint.setColor(session.card);
            canvas.drawCircle(left + dp(4) + circle / 2, top + height / 2, circle / 2, paint);
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setColor(section.color);
            paint.setTextSize(sp(section.code.length() > 2 ? 10 : 12));
            float baseline = top + height / 2 - (paint.ascent() + paint.descent()) / 2;
            canvas.drawText(section.code, left + dp(4) + circle / 2, baseline, paint);
            paint.setTextAlign(Paint.Align.LEFT); paint.setTextSize(textSize); paint.setColor(session.headerText);
            canvas.drawText(section.label, left + circle + dp(10), top + height / 2 - (paint.ascent() + paint.descent()) / 2, paint);
        }

        @Override public InputConnection onCreateInputConnection(EditorInfo attributes) {
            InputConnection connection = super.onCreateInputConnection(attributes);
            if (connection == null) return null;
            return new InputConnectionWrapper(connection, false) {
                void flushComposition() {
                    removeCallbacks(session.refreshAfterComposition);
                    post(session.refreshAfterComposition);
                }
                @Override public boolean finishComposingText() {
                    boolean result = super.finishComposingText();
                    flushComposition();
                    return result;
                }
                @Override public boolean commitText(CharSequence text, int newCursorPosition) {
                    boolean result = super.commitText(text, newCursorPosition);
                    flushComposition();
                    return result;
                }
                @Override public void closeConnection() {
                    super.closeConnection();
                    if (!session.ending) flushComposition();
                }
            };
        }

        @Override protected void onSelectionChanged(int start, int end) {
            super.onSelectionChanged(start, end);
            if (session != null && session.presentationPending && !session.styling
                    && BaseInputConnection.getComposingSpanStart(getText()) < 0) {
                removeCallbacks(session.refreshAfterComposition);
                post(session.refreshAfterComposition);
            }
        }

        @Override protected void onScrollChanged(int left, int top, int oldLeft, int oldTop) {
            super.onScrollChanged(left, top, oldLeft, oldTop);
            if (session == null || session.styling || getLayout() == null || sections.isEmpty()) return;
            // When every card fits, retain the last explicit destination.
            if (getLayout().getHeight() + getPaddingTop() + getPaddingBottom() <= getHeight()) return;
            String selected = "";
            for (SectionPresentation section : sections.values()) {
                RectF card = cardRectForTesting(section.id);
                if (card.isEmpty()) continue;
                if (card.top <= top + dp(52)) selected = section.id;
                else if (selected.isEmpty()) { selected = section.id; break; }
            }
            if (!selected.equals(session.activeSectionId)) {
                session.activeSectionId = selected; session.refreshNavigation();
            }
        }

        @Override public boolean onTextContextMenuItem(int id) {
            if (id == android.R.id.undo || id == android.R.id.redo) {
                session.historyMoving = true;
                session.historyDirection = id == android.R.id.undo ? -1 : 1;
                try { return super.onTextContextMenuItem(id); }
                finally { session.historyMoving = false; session.refreshPresentation(); }
            }
            return super.onTextContextMenuItem(id);
        }

        @Override public boolean onKeyShortcut(int keyCode, KeyEvent event) {
            if (keyCode == KeyEvent.KEYCODE_Z && event.isCtrlPressed())
                return onTextContextMenuItem(event.isShiftPressed() ? android.R.id.redo : android.R.id.undo);
            return super.onKeyShortcut(keyCode, event);
        }

        int dp(float value) { return session.dp(value); }
        float sp(float value) { return value * getResources().getDisplayMetrics().scaledDensity; }
    }

    static final class SectionSpacing implements LineHeightSpan, android.text.style.UpdateLayout {
        final CardEditText editor;
        final String id;
        SectionSpacing(CardEditText editor, String id) { this.editor = editor; this.id = id; }
        @Override public void chooseHeight(CharSequence text, int start, int end, int spanstartv,
                                           int v, Paint.FontMetricsInt metrics) {
            SectionPresentation section = editor.sections.get(id);
            if (section == null || start > section.start || end <= section.start) return;
            // Only ascent reserves space. The bottom used by native selection
            // handles stays on the actual text baseline, without card footer.
            int extra = editor.session.headingGap();
            metrics.ascent -= extra;
            metrics.top -= extra;
        }
    }

    static final class ParagraphGap implements LineHeightSpan, android.text.style.UpdateLayout {
        final CardEditText editor;
        final String id;
        int start;
        ParagraphGap(CardEditText editor, String id) { this.editor = editor; this.id = id; }
        @Override public void chooseHeight(CharSequence text, int lineStart, int lineEnd, int spanstartv,
                                           int v, Paint.FontMetricsInt metrics) {
            if (lineStart <= start && lineEnd > start) {
                int extra = editor.dp(16);
                metrics.ascent -= extra; metrics.top -= extra;
            }
        }
    }

    static final class LyricsCursor extends Drawable {
        final CursorState constantState;
        final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        LyricsCursor(CardEditText editor, int color) {
            this(new CursorState(editor, color));
        }
        private LyricsCursor(CursorState state) {
            constantState = state;
            paint.setColor(state.color);
        }
        @Override public void draw(Canvas canvas) {
            CardEditText editor = constantState.editor.get();
            if (editor == null) return;
            Rect bounds = getBounds();
            Layout layout = editor.getLayout();
            if (layout == null || editor.getSelectionStart() < 0) return;
            int line = layout.getLineForOffset(Math.max(0,
                    Math.min(Math.min(editor.length(), layout.getText().length()), editor.getSelectionStart())));
            int baseline = layout.getLineBaseline(line);
            Paint.FontMetricsInt metrics = editor.getPaint().getFontMetricsInt();
            canvas.drawRect(bounds.left, baseline + metrics.ascent,
                    Math.max(bounds.right, bounds.left + constantState.width), baseline + metrics.descent, paint);
        }
        @Override public void setAlpha(int alpha) { paint.setAlpha(alpha); invalidateSelf(); }
        @Override public int getAlpha() { return paint.getAlpha(); }
        @Override public void setColorFilter(android.graphics.ColorFilter filter) {
            paint.setColorFilter(filter); invalidateSelf();
        }
        @Override public android.graphics.ColorFilter getColorFilter() { return paint.getColorFilter(); }
        @Override public int getOpacity() { return android.graphics.PixelFormat.TRANSLUCENT; }
        @Override public int getIntrinsicWidth() { return constantState.width; }
        @Override public ConstantState getConstantState() { return constantState; }

        // Some OEM editors clone the cursor when creating native insertion or
        // selection handles. Supply independent drawables without retaining the
        // editor/Activity through a framework-cached ConstantState.
        static final class CursorState extends Drawable.ConstantState {
            final WeakReference<CardEditText> editor;
            final int color, width;
            CursorState(CardEditText editor, int color) {
                this.editor = new WeakReference<>(editor);
                this.color = color;
                width = editor.dp(2);
            }
            @Override public Drawable newDrawable() { return new LyricsCursor(this); }
            @Override public Drawable newDrawable(android.content.res.Resources resources) {
                return new LyricsCursor(this);
            }
            @Override public Drawable newDrawable(android.content.res.Resources resources,
                                                   android.content.res.Resources.Theme theme) {
                return new LyricsCursor(this);
            }
            @Override public int getChangingConfigurations() { return 0; }
        }
    }

    static final class SectionButton extends View {
        final EditorSession session;
        final SectionPresentation section;
        final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        SectionButton(Context context, EditorSession session, SectionPresentation section) {
            super(context); this.session = session; this.section = section;
            setClickable(true); setFocusable(true);
            setTag("nativeLyricsSection-" + section.id);
            setContentDescription(section.label);
        }
        @Override protected void onDraw(Canvas canvas) {
            float cx = getWidth() / 2f, cy = getHeight() / 2f, radius = Math.max(session.dp(18), session.sp(13)/2f + session.dp(10));
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(isSelected() ? ((section.color & 0xffffff) | 0x26000000) : Color.TRANSPARENT);
            canvas.drawCircle(cx, cy, radius, paint);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(session.dp(isSelected() ? 3 : 2));
            paint.setColor(section.color); canvas.drawCircle(cx, cy, radius - session.dp(1), paint);
            paint.setStyle(Paint.Style.FILL); paint.setColor(section.color);
            paint.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
            paint.setTextSize(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP,
                    section.code.length() > 2 ? 11 : 13, getResources().getDisplayMetrics()));
            paint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText(section.code, cx, cy - (paint.ascent() + paint.descent()) / 2, paint);
        }
    }

    static final class ArrowButton extends View {
        final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        ArrowButton(Context context, int foreground) {
            super(context); paint.setColor(foreground); paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeCap(Paint.Cap.ROUND); paint.setStrokeJoin(Paint.Join.ROUND);
            setClickable(true); setFocusable(true); setBackground(selectable(context));
        }
        @Override protected void onDraw(Canvas canvas) {
            float d = getResources().getDisplayMetrics().density;
            float cx = getWidth() / 2f, cy = getHeight() / 2f;
            paint.setStrokeWidth(2 * d);
            Path arrow = new Path(); arrow.moveTo(cx + 5*d, cy - 8*d); arrow.lineTo(cx - 3*d, cy);
            arrow.lineTo(cx + 5*d, cy + 8*d); canvas.drawPath(arrow, paint);
            canvas.drawLine(cx - 3*d, cy, cx + 12*d, cy, paint);
        }
    }

    static final class PageLayout extends FrameLayout {
        final int topColor, bottomColor, accent;
        final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        Shader gradient, glow;
        PageLayout(Context context, int top, int bottom, int accent) {
            super(context); topColor = top; bottomColor = bottom; this.accent = accent;
            setWillNotDraw(false);
        }
        @Override protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
            super.onSizeChanged(width, height, oldWidth, oldHeight);
            gradient = new LinearGradient(0, 0, 0, Math.max(1, height), topColor, bottomColor, Shader.TileMode.CLAMP);
            glow = new RadialGradient(width*.16f, height*.18f,
                    Math.max(1, Math.max(width, height)*.68f),
                    (accent & 0xffffff) | 0x0f000000, accent & 0xffffff, Shader.TileMode.CLAMP);
        }
        @Override protected void onDraw(Canvas canvas) {
            paint.setShader(gradient);
            canvas.drawRect(0, 0, getWidth(), getHeight(), paint);
            paint.setShader(glow);
            canvas.drawRect(0, 0, getWidth(), getHeight(), paint); paint.setShader(null);
        }
    }

    static final class UnicodeLengthFilter implements InputFilter {
        final int limit;
        UnicodeLengthFilter(int limit) { this.limit = limit; }
        @Override public CharSequence filter(CharSequence source, int start, int end,
                                               Spanned dest, int dstart, int dend) {
            int available = limit - (dest.length() - (dend - dstart));
            if (available >= end - start) return null;
            if (available <= 0) return "";
            int last = start + available;
            if (last > start && Character.isHighSurrogate(source.charAt(last - 1))) --last;
            return source.subSequence(start, last);
        }
    }

    static TextView text(Context context, String value, float size, int color, boolean medium) {
        TextView view = new TextView(context);
        view.setText(value); view.setTextSize(TypedValue.COMPLEX_UNIT_SP, size); view.setTextColor(color);
        view.setTypeface(Typeface.create(medium ? "sans-serif-medium" : "sans-serif", Typeface.NORMAL));
        view.setIncludeFontPadding(false);
        return view;
    }
    static Drawable tinted(Drawable source, int color) {
        Drawable result = source.mutate();
        result.setTint(color);
        return result;
    }
    static Drawable selectable(Context context) {
        TypedValue value = new TypedValue();
        context.getTheme().resolveAttribute(android.R.attr.selectableItemBackgroundBorderless, value, true);
        return value.resourceId == 0 ? new ColorDrawable(Color.TRANSPARENT) : context.getDrawable(value.resourceId);
    }
    static int color(JSONObject colors, String key, int fallback) {
        return parseColor(colors == null ? "" : colors.optString(key), fallback);
    }
    static int parseColor(String value, int fallback) {
        try { return Color.parseColor(value); } catch (IllegalArgumentException ignored) { return fallback; }
    }
    static boolean isNewline(char value) { return value == '\n' || value == '\r' || value == 0x85 || value == 0x2028 || value == 0x2029; }
    static double luminance(int color) {
        return (.2126 * Color.red(color) + .7152 * Color.green(color) + .0722 * Color.blue(color)) / 255.;
    }
}
