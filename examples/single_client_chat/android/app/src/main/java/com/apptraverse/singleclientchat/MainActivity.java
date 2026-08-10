package com.apptraverse.singleclientchat;

import android.app.Activity;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Single Activity of the example. It holds no model state: the transcript and
 * the status come from the native runtime through {@link NativeUiBridge}.
 */
public final class MainActivity extends Activity implements NativeUiBridge.Listener {

  private TextView statusView;
  private ScrollView transcriptScroll;
  private TextView transcriptView;
  private EditText messageInput;
  private Button sendButton;

  // Text waiting for the native MESSAGE_COMMITTED confirmation.
  private String pendingText;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    statusView = findViewById(R.id.status);
    transcriptScroll = findViewById(R.id.transcript_scroll);
    transcriptView = findViewById(R.id.transcript);
    messageInput = findViewById(R.id.message_input);
    sendButton = findViewById(R.id.send);

    sendButton.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View view) {
        submit();
      }
    });

    messageInput.setOnEditorActionListener(new TextView.OnEditorActionListener() {
      @Override
      public boolean onEditorAction(TextView view, int actionId, KeyEvent event) {
        submit();
        return true;
      }
    });
  }

  @Override
  protected void onStart() {
    super.onStart();
    // Keeps the input focused so adb shell input text reaches it.
    messageInput.requestFocus();
    application().uiBridge().attach(this);
    application().requestSnapshot();
  }

  @Override
  protected void onStop() {
    application().uiBridge().detach(this);
    super.onStop();
  }

  @Override
  public void onStatus(String status) {
    statusView.setText(status);
  }

  @Override
  public void onTranscript(String transcript) {
    transcriptView.setText(transcript);
    transcriptScroll.post(new Runnable() {
      @Override
      public void run() {
        transcriptScroll.fullScroll(View.FOCUS_DOWN);
      }
    });
  }

  @Override
  public void onMessageCommitted(String text) {
    if (pendingText != null && pendingText.equals(text)) {
      messageInput.setText("");
      pendingText = null;
    }
  }

  private void submit() {
    String text = messageInput.getText().toString().trim();
    if (text.isEmpty()) {
      return;
    }

    pendingText = text;
    application().send(text);
  }

  private SingleClientChatApplication application() {
    return (SingleClientChatApplication) getApplication();
  }
}
