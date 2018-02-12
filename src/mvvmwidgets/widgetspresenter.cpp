#include "widgetspresenter.h"
#include "widgetspresenter_p.h"
#include "ipresentingview.h"

#include <QtMvvmCore/private/coreapp_p.h>
#include <QtMvvmCore/private/qtmvvm_logging_p.h>

#include <QtWidgets/QDialog>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QMdiSubWindow>
#include <QtWidgets/QMdiArea>
#include <QtWidgets/QApplication>

using namespace QtMvvm;

WidgetsPresenter::WidgetsPresenter(QObject *parent) :
	QObject(parent),
	IPresenter(),
	d(new WidgetsPresenterPrivate())
{}

WidgetsPresenter::~WidgetsPresenter() {}

void WidgetsPresenter::registerView(const QMetaObject *viewType)
{
	Q_ASSERT_X(viewType->inherits(&QWidget::staticMetaObject), Q_FUNC_INFO, "viewType must be a QWidget class");
	WidgetsPresenterPrivate::currentPresenter()->d->implicitMappings.insert(viewType);
}

void WidgetsPresenter::registerViewExplicitly(const QMetaObject *viewModelType, const QMetaObject *viewType)
{
	Q_ASSERT_X(viewModelType->inherits(&ViewModel::staticMetaObject), Q_FUNC_INFO, "viewModelType must be a QtMvvm::ViewModel class");
	Q_ASSERT_X(viewType->inherits(&QWidget::staticMetaObject), Q_FUNC_INFO, "viewType must be a QWidget class");
	WidgetsPresenterPrivate::currentPresenter()->d->explicitMappings.insert(viewModelType->className(), viewType);
}

void WidgetsPresenter::present(ViewModel *viewModel, const QVariantHash &params, QPointer<ViewModel> parent)
{
	// find and create view
	auto viewMetaObject = findWidgetMetaObject(viewModel->metaObject());
	if(!viewMetaObject)
		throw PresenterException("Unable to find view any matching view");

	auto parentView = parent ?
						  qobject_cast<QWidget*>(parent->parent()) :
						  nullptr;
	auto view = qobject_cast<QWidget*>(viewMetaObject->newInstance(Q_ARG(QtMvvm::ViewModel*, viewModel),
																   Q_ARG(QWidget*, parentView)));
	if(!view) {
		throw PresenterException(QByteArrayLiteral("Failed to create view of type") +
								 viewMetaObject->className() +
								 QByteArrayLiteral("(did you mark the constructor as Q_INVOKABLE? "
												   "Required signature: \"Q_INVOKABLE Contructor(QtMvvm::ViewModel *, QWidget*);\")"));
	}

	// initialize viewmodel and view relationship
	viewModel->setParent(view);
	auto hasCycle = setupLifeCycle(viewModel, view);
	if(!hasCycle)
		view->setAttribute(Qt::WA_DeleteOnClose);
	viewModel->onInit(params);

	// present the view
	auto presented = false;
	auto tPresenter = dynamic_cast<IPresentingView*>(parentView);
	if(tPresenter)
		presented = tPresenter->tryPresent(view);
	if(!presented)
		presented = tryPresent(view, parentView);

	//handle the present result
	if(!presented) {
		view->deleteLater();
		throw PresenterException(QByteArrayLiteral("Unable to find a method that is able to present a view of type") +
								 viewMetaObject->className());
	}
}

void WidgetsPresenter::showDialog(const MessageConfig &config, MessageResult *result)
{
	Q_UNIMPLEMENTED();
}

const QMetaObject *WidgetsPresenter::findWidgetMetaObject(const QMetaObject *viewModelMetaObject)
{
	auto currentMeta = viewModelMetaObject;
	while(currentMeta && currentMeta->inherits(&ViewModel::staticMetaObject)) {
		QByteArray cName = currentMeta->className();
		if(d->explicitMappings.contains(cName))
			return d->explicitMappings.value(cName);
		else {
			auto lIndex = cName.lastIndexOf("ViewModel");
			if(lIndex > 0)
				cName.truncate(lIndex);
			for(auto metaObject : d->implicitMappings) {
				QByteArray vName = metaObject->className();
				if(vName.startsWith(cName))
					return metaObject;
			}
		}

		currentMeta = currentMeta->superClass();
	}

	return nullptr;
}

bool WidgetsPresenter::tryPresent(QWidget *view, QWidget *parentView)
{
	auto metaObject = view->metaObject();

	// Check if QDialog
	if(metaObject->inherits(&QDialog::staticMetaObject)) {
		qobject_cast<QDialog*>(view)->open();
		return true;
	}

	//Check if QMainWindow
	QWidget *central = nullptr;
	if(parentView && parentView->metaObject()->inherits(&QMainWindow::staticMetaObject)) {
		auto mainWindow = qobject_cast<QMainWindow*>(parentView);

		// Check if child is QDockWidget
		if(metaObject->inherits(&QDockWidget::staticMetaObject)) {
			auto dockWidget = qobject_cast<QDockWidget*>(view);
			if(!mainWindow->restoreDockWidget(dockWidget))
				mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dockWidget);
			return true;
		}

		//extract central widget for futher parts
		central = mainWindow->centralWidget();
	}

	// for both, the parent and the central view, depending on which is available
	for(auto pView : { parentView, central }) {
		if(!pView)
			continue;

		// Check if QMdiArea and child is QMdiSubWindow
		if(pView->metaObject()->inherits(&QMdiArea::staticMetaObject) &&
		   metaObject->inherits(&QMdiSubWindow::staticMetaObject)) {
			auto mdiArea = qobject_cast<QMdiArea*>(pView);
			mdiArea->addSubWindow(view);
			return true;
		}
	}

	//none of the special cases -> simply "show" it
	showForeground(view);
	return true;
}

bool WidgetsPresenter::setupLifeCycle(ViewModel *viewModel, QWidget *view)
{
	//TODO implement as soon as quick part is ready as well
	Q_UNUSED(viewModel)
	Q_UNUSED(view)
	return false;
}

void WidgetsPresenter::showForeground(QWidget *view) const
{
	if(view->windowState().testFlag(Qt::WindowMinimized))
		view->setWindowState(view->windowState() & ~Qt::WindowMinimized);
	view->show();
	view->raise();
	QApplication::alert(view);
	view->activateWindow();
}

// ------------- Private Implementation -------------

WidgetsPresenterPrivate::WidgetsPresenterPrivate() :
	implicitMappings(),
	explicitMappings()
{}

WidgetsPresenter *WidgetsPresenterPrivate::currentPresenter()
{
#ifndef Q_NO_DEBUG
	Q_ASSERT_X(dynamic_cast<WidgetsPresenter*>(CoreAppPrivate::dInstance()->currentPresenter()),
			   Q_FUNC_INFO,
			   "Cannot register widgets if the current presenter does not extend QtMvvm::WidgetsPresenter");
#endif
	auto presenter = static_cast<WidgetsPresenter*>(CoreAppPrivate::dInstance()->currentPresenter());
	if(!presenter) {
		presenter = new WidgetsPresenter();
		CoreApp::setMainPresenter(presenter);
	}
	return presenter;
}
