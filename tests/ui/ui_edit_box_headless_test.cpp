#include <cassert>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_edit_box.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_window.h"

using namespace apptraverse;
using namespace apptraverse::ui;

namespace {

UiEditBox::ptr MakeEditBox(ae::Domain& domain) {
  auto box = UiEditBox::ptr::Create(ae::CreateWith{domain});
  return box;
}

}  // namespace

int main() {
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();
  EnsureUiPresenterRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  auto edit_box = MakeEditBox(domain);
  InitializeRuntimeNode(*edit_box, *doc);
  edit_box->SubmitTextFromUi("hello", 5);
  if (edit_box->text() != "hello" || edit_box->caret_utf8_offset() != 5) {
    std::cerr << "edit box apply failed\n";
    return 1;
  }

  bool posted = false;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    posted = true;
    work(domain);
  }};

  auto presenter = UiEditBoxPresenter::ptr::Create(ae::CreateWith{domain});
  presenter->edit_box = edit_box;
  presenter->model_proxy = &proxy;
  presenter->SubmitTextFromUi("mirror", 3);
  if (!posted || edit_box->text() != "mirror") {
    std::cerr << "presenter proxy path failed\n";
    return 2;
  }

  return 0;
}
