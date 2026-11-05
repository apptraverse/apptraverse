Android presenter classes (`android_ui_presenters.cpp`, `android_ui_registration.cpp`,
target `apptraverse_ui_android_presenters`) carry ClassId, fields and links without JNI
and build on the build host, where the MinimalApp supply exporter distills them.
Their native Views are implemented by `AndroidPresentationHost`
(`android_ui_*_presenter.cpp`, target `apptraverse_ui_android`), passed at runtime as
`Presenter::presentation_host` through `AndroidUiNativeViews`.
