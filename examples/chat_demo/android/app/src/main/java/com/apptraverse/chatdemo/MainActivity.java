package com.apptraverse.chatdemo;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

/** Placeholder host until Commit 12 wires native Android widgets. */
public final class MainActivity extends Activity {
  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    TextView loading = new TextView(this);
    loading.setText(R.string.loading);
    setContentView(loading);
  }
}
