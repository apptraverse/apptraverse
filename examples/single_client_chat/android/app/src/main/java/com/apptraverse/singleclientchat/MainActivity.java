package com.apptraverse.singleclientchat;

import android.app.Activity;
import android.content.res.Configuration;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Single Activity of the example. It holds no model state: the transcript comes
 * from the native runtime through {@link NativeUiBridge}. Orientation follows
 * the system auto-rotate setting; the Activity is recreated on rotation and
 * never owns the NativeRuntime.
 */
public final class MainActivity extends Activity implements NativeUiBridge.Listener {

  private static final String TAG = "AppTraverseChat";

  private ScrollView transcriptScroll;
  private TextView transcriptView;
  private EditText messageInput;
  private Button sendButton;

  private int lastViewportWidth;
  private int lastViewportHeight;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

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
        if (event != null && event.getAction() != KeyEvent.ACTION_DOWN) {
          return true;
        }
        submit();
        return true;
      }
    });

    Log.i(TAG, "ACTIVITY_CREATED instance=" + identity()
        + " orientation=" + orientationName());

    final View root = findViewById(android.R.id.content);
    root.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
      @Override
      public void onLayoutChange(
          View view,
          int left,
          int top,
          int right,
          int bottom,
          int oldLeft,
          int oldTop,
          int oldRight,
          int oldBottom) {
        int width = right - left;
        int height = bottom - top;
        if (width <= 0 || height <= 0) {
          return;
        }
        if (width == lastViewportWidth && height == lastViewportHeight) {
          return;
        }
        lastViewportWidth = width;
        lastViewportHeight = height;
        DisplayMetrics metrics = getResources().getDisplayMetrics();
        int densityDpi = metrics.densityDpi;
        Log.i(TAG, "ACTIVITY_VIEWPORT instance=" + identity()
            + " width=" + width + " height=" + height
            + " dpi=" + densityDpi);
        application().queueWindowChanged(width, height, densityDpi);
      }
    });
  }

  @Override
  protected void onStart() {
    super.onStart();
    Log.i(TAG, "ACTIVITY_STARTED instance=" + identity());
    messageInput.requestFocus();
    application().uiBridge().attach(this);
  }

  @Override
  protected void onStop() {
    Log.i(TAG, "ACTIVITY_STOPPED instance=" + identity());
    application().uiBridge().detach(this);
    super.onStop();
  }

  @Override
  protected void onDestroy() {
    Log.i(TAG, "ACTIVITY_DESTROYED instance=" + identity());
    super.onDestroy();
  }

  @Override
  public void onTranscript(String transcript) {
    final boolean inputHadFocus = messageInput.hasFocus();
    transcriptView.setText(transcript);
    transcriptScroll.post(new Runnable() {
      @Override
      public void run() {
        // Scroll without focus navigation (fullScroll(FOCUS_DOWN) can steal keyboard focus).
        transcriptScroll.scrollTo(0, transcriptView.getHeight());
        if (inputHadFocus && !messageInput.hasFocus()) {
          messageInput.requestFocus();
        }
      }
    });
  }

  private void submit() {
    String text = messageInput.getText().toString().trim();
    if (text.isEmpty()) {
      return;
    }
    application().send(text);
    messageInput.setText("");
  }

  private String identity() {
    return Integer.toHexString(System.identityHashCode(this));
  }

  private String orientationName() {
    int orientation = getResources().getConfiguration().orientation;
    if (orientation == Configuration.ORIENTATION_LANDSCAPE) {
      return "landscape";
    }
    if (orientation == Configuration.ORIENTATION_PORTRAIT) {
      return "portrait";
    }
    return "undefined";
  }

  private SingleClientChatApplication application() {
    return (SingleClientChatApplication) getApplication();
  }
}
