Android presenter classes (`android_ui_presenters.cpp`, `android_ui_registration.cpp`,
target `apptraverse_ui_android_presenters`) carry ClassId, fields and links without JNI
and build on the build host, where the MinimalApp supply exporter distills them.
Their native Views are implemented by `AndroidPresentationHost`
(`android_ui_*_presenter.cpp`, target `apptraverse_ui_android`), passed at runtime as
`Presenter::presentation_host` through `AndroidUiNativeViews`.

`APPTRAVERSE_ANDROID_TEXT_CONTROLS` defaults to `ON`. A consumer with no
text-control graph can select `OFF` before adding AppTraverse; this excludes the
text presenter, its factory, the JNI View adapter and the concrete host's text
virtual methods. WindowCatalog selects this narrower host; MinimalApp retains
the default full interface. The public target definition keeps the host ABI
consistent in backend and consumer translation units; persistent schemas do not
change.
