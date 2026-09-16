package com.apptraverse.chatdemo;

import android.app.Activity;
import android.content.res.Configuration;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.TextView;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Native Android widgets for chat delivery. Portrait shows list then
 * conversation; landscape uses master-detail. Private UI state is retained in
 * {@link ChatUiRetention} on the Application across config changes.
 */
public final class MainActivity extends Activity implements NativeUiBridge.Listener {

  private static final String TAG = "AppTraverseChat";

  private LinearLayout listPanel;
  private LinearLayout conversationPanel;
  private LinearLayout contentRow;
  private ListView chatList;
  private TextView transcript;
  private ScrollView transcriptScroll;
  private EditText draft;
  private EditText adminId;
  private EditText aetherUid;
  private Button openPeer;
  private Button sendDraft;
  private TextView statusLine;
  private TextView presenceLine;

  private final List<Long> entryIds = new ArrayList<>();
  private final List<String> entryLabels = new ArrayList<>();
  private ArrayAdapter<String> listAdapter;

  private boolean applyingNativeState;
  private boolean wideLayout;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    listPanel = findViewById(R.id.list_panel);
    conversationPanel = findViewById(R.id.conversation_panel);
    contentRow = findViewById(R.id.content_row);
    chatList = findViewById(R.id.chat_list);
    transcript = findViewById(R.id.transcript);
    transcriptScroll = findViewById(R.id.transcript_scroll);
    draft = findViewById(R.id.draft);
    adminId = findViewById(R.id.admin_id);
    aetherUid = findViewById(R.id.aether_uid);
    openPeer = findViewById(R.id.open_peer);
    sendDraft = findViewById(R.id.send_draft);
    statusLine = findViewById(R.id.status_line);
    presenceLine = findViewById(R.id.presence_line);

    listAdapter = new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, entryLabels);
    chatList.setAdapter(listAdapter);
    chatList.setOnItemClickListener(new AdapterView.OnItemClickListener() {
      @Override
      public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
        if (position < 0 || position >= entryIds.size()) {
          return;
        }
        flushDraftAndScroll();
        long entryId = entryIds.get(position);
        app().selectChat(entryId);
        retention().selectedEntryId = entryId;
        if (!wideLayout) {
          retention().conversationVisibleInPortrait = true;
          applyLayoutMode();
        }
      }
    });

    openPeer.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View v) {
        byte[] admin = utf8Bytes(adminId.getText().toString());
        if (admin.length == 0) {
          return;
        }
        byte[] uid = utf8Bytes(aetherUid.getText().toString());
        app().openPeer(admin, uid.length == 0 ? null : uid);
      }
    });

    sendDraft.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View v) {
        long entryId = retention().selectedEntryId;
        if (entryId == 0L) {
          return;
        }
        byte[] text = utf8Bytes(draft.getText().toString());
        if (text.length == 0) {
          return;
        }
        ++retention().draftEditRevision;
        app().sendDraft(entryId, text, retention().draftEditRevision);
      }
    });

    draft.addTextChangedListener(new TextWatcher() {
      @Override
      public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

      @Override
      public void onTextChanged(CharSequence s, int start, int before, int count) {
        if (applyingNativeState || retention().selectedEntryId == 0L) {
          return;
        }
        ++retention().draftEditRevision;
        app().editDraft(retention().selectedEntryId, utf8Bytes(s.toString()),
            retention().draftEditRevision);
      }

      @Override
      public void afterTextChanged(Editable s) {}
    });

    contentRow.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
      @Override
      public void onLayoutChange(View v, int left, int top, int right, int bottom,
                                 int oldLeft, int oldTop, int oldRight, int oldBottom) {
        updateWideLayout(right - left, bottom - top);
      }
    });

    Log.i(TAG, "ACTIVITY_CREATED");
  }

  @Override
  public void onConfigurationChanged(Configuration newConfig) {
    super.onConfigurationChanged(newConfig);
    contentRow.post(new Runnable() {
      @Override
      public void run() {
        updateWideLayout(contentRow.getWidth(), contentRow.getHeight());
      }
    });
  }

  @Override
  protected void onStart() {
    super.onStart();
    app().uiBridge().attach(this);
  }

  @Override
  protected void onStop() {
    if (!isFinishing()) {
      flushDraftAndScroll();
      app().checkpoint();
    }
    app().uiBridge().detach(this);
    super.onStop();
  }

  @Override
  public void onBackPressed() {
    if (!wideLayout && retention().conversationVisibleInPortrait) {
      retention().conversationVisibleInPortrait = false;
      applyLayoutMode();
      return;
    }
    app().requestStop();
  }

  @Override
  public void onUiState(NativeUiBridge.ChatUiState state) {
    applyingNativeState = true;

    entryIds.clear();
    entryLabels.clear();
    for (int i = 0; i < state.entryIds.length; ++i) {
      entryIds.add(state.entryIds[i]);
      entryLabels.add(new String(state.entryLabels[i], StandardCharsets.UTF_8));
    }
    listAdapter.notifyDataSetChanged();

    if (state.selectedEntryId != 0L) {
      retention().selectedEntryId = state.selectedEntryId;
      for (int i = 0; i < entryIds.size(); ++i) {
        if (entryIds.get(i) == state.selectedEntryId) {
          chatList.setSelection(i);
          break;
        }
      }
    }

    String draftText = new String(state.draft, StandardCharsets.UTF_8);
    if (!draftText.contentEquals(draft.getText())) {
      draft.setText(draftText);
    }

    transcript.setText(new String(state.transcript, StandardCharsets.UTF_8));
    statusLine.setText(new String(state.status, StandardCharsets.UTF_8));
    presenceLine.setText(new String(state.presence, StandardCharsets.UTF_8));
    sendDraft.setEnabled(state.sendEnabled);

    retention().followTail = state.followTail;
    retention().scrollMsgUid = state.scrollMsgUid;
    retention().scrollMsgSeq = state.scrollMsgSeq;
    retention().scrollOffset = state.scrollOffset;

    if (state.followTail) {
      transcriptScroll.post(new Runnable() {
        @Override
        public void run() {
          transcriptScroll.fullScroll(View.FOCUS_DOWN);
        }
      });
    }

    applyingNativeState = false;
    updateWideLayout(contentRow.getWidth(), contentRow.getHeight());
  }

  @Override
  public void onModelStopped() {
    finishAndRemoveTask();
  }

  private void updateWideLayout(int width, int height) {
    boolean wide = width > height && width > 0;
    if (wide != wideLayout) {
      wideLayout = wide;
      app().setWideLayout(wide);
      applyLayoutMode();
    }
  }

  private void applyLayoutMode() {
    if (wideLayout) {
      listPanel.setVisibility(View.VISIBLE);
      conversationPanel.setVisibility(View.VISIBLE);
      LinearLayout.LayoutParams listParams =
          (LinearLayout.LayoutParams) listPanel.getLayoutParams();
      listParams.weight = 1f;
      listPanel.setLayoutParams(listParams);
      LinearLayout.LayoutParams convParams =
          (LinearLayout.LayoutParams) conversationPanel.getLayoutParams();
      convParams.weight = 2f;
      conversationPanel.setLayoutParams(convParams);
    } else if (retention().conversationVisibleInPortrait && retention().selectedEntryId != 0L) {
      listPanel.setVisibility(View.GONE);
      conversationPanel.setVisibility(View.VISIBLE);
    } else {
      listPanel.setVisibility(View.VISIBLE);
      conversationPanel.setVisibility(View.GONE);
    }
  }

  private void flushDraftAndScroll() {
    long entryId = retention().selectedEntryId;
    if (entryId == 0L) {
      return;
    }
    byte[] draftBytes = utf8Bytes(draft.getText().toString());
    ++retention().draftEditRevision;
    app().editDraft(entryId, draftBytes, retention().draftEditRevision);
    app().saveScroll(entryId, retention().followTail, retention().scrollMsgUid,
        retention().scrollMsgSeq, retention().scrollOffset);
  }

  private static byte[] utf8Bytes(String text) {
    return text.getBytes(StandardCharsets.UTF_8);
  }

  private ChatApplication app() {
    return (ChatApplication) getApplication();
  }

  private ChatUiRetention retention() {
    return app().uiRetention();
  }
}
