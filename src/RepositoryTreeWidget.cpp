#include "RepositoryTreeWidget.h"
#include <QDropEvent>
#include <QDebug>
#include <QMimeData>
#include <QApplication>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QFileInfo>
#include "MainWindow.h"

static int u16ncmp(ushort const *s1, ushort const *s2, int n)
{
	for (int i = 0; i < n; i++) {
		ushort c1 = s1[i];
		ushort c2 = s2[i];
		if (c1 < 128) c1 = toupper(c1);
		if (c2 < 128) c2 = toupper(c2);
		if (c1 != c2) {
			return c1 - c2;
		}
	}
	return 0;
}

class RepositoryTreeWidgetItemDelegate : public QStyledItemDelegate {
public:
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
	{
#if 1
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);

		RepositoryTreeWidget const *treewidget = qobject_cast<RepositoryTreeWidget const *>(opt.widget);
		Q_ASSERT(treewidget);
		QString filtertext = treewidget->getFilterText();

		QRect iconrect = opt.widget->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &opt, opt.widget);
		QRect textrect = opt.widget->style()->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);

		// 背景を描画
		if (!filtertext.isEmpty()) {
			painter->fillRect(opt.rect, QColor(128, 128, 128, 64));
		}
		opt.widget->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

		// アイコンを描画
		opt.icon.paint(painter, iconrect);

		// テキストを描画
		std::vector<std::tuple<QString, bool>> list;
		if (filtertext.isEmpty()) { // フィルターがない場合
			list.push_back(std::make_tuple(opt.text, false));
		} else {
			// フィルターがある場合
			const int filtersize = filtertext.size();
			int left = 0;
			int right = 0;
			while (right < opt.text.size()) { // テキストをフィルターで分割
				if (u16ncmp((ushort const *)opt.text.utf16() + right, (ushort const *)filtertext.utf16(), filtersize) == 0) {
					if (left < right) {
						list.push_back(std::make_tuple(opt.text.mid(left, right - left), false));
					}
					list.push_back(std::make_tuple(opt.text.mid(right, filtersize), true));
					left = right = right + filtersize;
				} else {
					right++;
				}
			}
			if (left < right) { // フィルターで分割できなかった残り
				list.push_back(std::make_tuple(opt.text.mid(left, right - left), false));
			}
		}
		int x = textrect.x();
		for (auto [s, f] : list) {
			int w = painter->fontMetrics().size(Qt::TextSingleLine, s).width();
			QRect r = textrect;
			r.setLeft(x);
			r.setWidth(w);
			if (f) { // フィルターの部分をハイライト
				QColor color = opt.palette.color(QPalette::Highlight);
				color.setAlpha(128);
				painter->fillRect(r, color);
			}
#ifndef Q_OS_WIN
			if (opt.state & QStyle::State_Selected) { // 選択されている場合
				painter->setPen(opt.palette.color(QPalette::HighlightedText));
			} else {
				painter->setPen(opt.palette.color(QPalette::Text));
			}
#endif
			painter->drawText(r, opt.displayAlignment, s); // テキストを描画
			x += w;
		}
#else
		QStyledItemDelegate::paint(painter, option, index);
#endif
	}
};

struct RepositoryTreeWidget::Private {
	RepositoryTreeWidgetItemDelegate delegate;
	QString filter_text;
};

RepositoryTreeWidget::RepositoryTreeWidget(QWidget *parent)
	: QTreeWidget(parent)
	, m(new Private)
{
	setItemDelegate(&m->delegate);
	connect(this, &RepositoryTreeWidget::currentItemChanged, [&](QTreeWidgetItem *current, QTreeWidgetItem *){
		current_item = current;
	});
}

RepositoryTreeWidget::~RepositoryTreeWidget()
{
	delete m;
}

QTreeWidgetItem *RepositoryTreeWidget::newQTreeWidgetItem()
{
	auto *item = new QTreeWidgetItem;
	item->setSizeHint(0, QSize(20, 20));
	return item;
}

QTreeWidgetItem *RepositoryTreeWidget::newQTreeWidgetFolderItem(const QString &name)
{
	QTreeWidgetItem *item = newQTreeWidgetItem();
	item->setText(0, name);
	item->setData(0, MainWindow::IndexRole, MainWindow::GroupItem);
	item->setIcon(0, global->graphics->folder_icon);
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	return item;
}

void RepositoryTreeWidget::setFilterText(const QString &filtertext)
{
	m->filter_text = filtertext;
	update();
}

QString RepositoryTreeWidget::getFilterText() const
{
	return m->filter_text;
}

void RepositoryTreeWidget::paintEvent(QPaintEvent *event)
{
	QTreeWidget::paintEvent(event);
}

void RepositoryTreeWidget::setRepositoryListStyle(RepositoryListStyle style)
{
	current_repository_list_style_ = style;
}

void RepositoryTreeWidget::updateList(RepositoryListStyle style, QList<RepositoryData> const &repos, const QString &filter)
{
	RepositoryTreeWidget *tree = this;
	tree->clear();

	// 新しいツリーウィジェットアイテムを作成
	auto NewItem = [&](QString const &reponame, int index){
		QTreeWidgetItem *item = newQTreeWidgetItem();
		item->setText(0, reponame);
		item->setData(0, MainWindow::IndexRole, index);
		item->setIcon(0, global->graphics->repository_icon);
		item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
		return item;
	};

	//
	auto EnableDragAndDropOnRepositoryTree = [&](bool enabled){
		tree->setDragEnabled(enabled);
		tree->setAcceptDrops(enabled);
		tree->setDropIndicatorShown(enabled);
		tree->viewport()->setAcceptDrops(enabled);
	};

	auto newQTreeWidgetFolderItem = [](QString const &name)->QTreeWidgetItem *{
		QTreeWidgetItem *item = newQTreeWidgetItem();
		item->setText(0, name);
		item->setData(0, MainWindow::IndexRole, MainWindow::GroupItem);
		item->setIcon(0, global->graphics->folder_icon);
		item->setFlags(item->flags() | Qt::ItemIsEditable);
		return item;
	};

	// リポジトリリストを更新（標準）
	auto UpdateRepositoryListStandard = [&](QString const &filter){

		EnableDragAndDropOnRepositoryTree(filter.isEmpty()); // フィルタが空の場合はドラッグ＆ドロップを有効にする
		tree->setFilterText(filter);

		std::map<QString, QTreeWidgetItem *> parentmap;

		for (int i = 0; i < repos.size(); i++) {
			RepositoryData const &repo = repos.at(i);

			if (!filter.isEmpty()) { // フィルターによる検索
				if (repo.name.indexOf(filter, 0, Qt::CaseInsensitive) < 0) continue; // 該当しない場合はスキップ
			}

			QTreeWidgetItem *parent = nullptr;
			{
				QString groupname = repo.group;
				if (groupname.startsWith('/')) { // 先頭のスラッシュを削除
					groupname = groupname.mid(1);
				}
				if (groupname == "") { // グループ名が空の場合はデフォルトに設定
					groupname = "Default";
				}
				auto it = parentmap.find(groupname); // 親アイテムを取得
				if (it != parentmap.end()) {
					parent = it->second;
				} else { // 親アイテムが見つからない場合は新規作成
					QStringList list = groupname.split('/', _SkipEmptyParts);
					if (list.isEmpty()) {
						list.push_back("Default");
					}
					QString groupPath;
					for (QString const &name : list) {
						if (name.isEmpty()) continue;
						QString groupPathWithCurrent = groupPath + name; // グループパスに現在のグループ名を追加
						auto it = parentmap.find(groupPathWithCurrent);
						if (it != parentmap.end()) {
							parent = it->second;
						} else {
							QTreeWidgetItem *newitem = newQTreeWidgetFolderItem(name);
							if (!parent) {
								tree->addTopLevelItem(newitem); // トップレベルに追加
							} else {
								parent->addChild(newitem); // 親の子
							}
							parent = newitem;
							parentmap[groupPathWithCurrent] = newitem; // parentmapに登録
							newitem->setExpanded(true); // 展開
						}
						groupPath = groupPathWithCurrent + '/';
					}
					Q_ASSERT(parent);
				}
			}
			parent->setData(0, MainWindow::FilePathRole, "");

			auto *child = NewItem(repo.name, i);
			parent->addChild(child);
			parent->setExpanded(true);
		}
	};

	// リポジトリリストを更新（最終更新日時順）
	auto UpdateRepositoryListSortRecent = [&](QString const &filter){

		EnableDragAndDropOnRepositoryTree(false); // ドラッグ＆ドロップを無効にする
		tree->setFilterText({});

		struct Item {
			RepositoryData const *data;
			QFileInfo info;
		};
		std::vector<Item> items;
		{
			for (RepositoryData const &item : repos) {
				QString gitpath = item.local_dir / ".git";
				QFileInfo info(gitpath);
				items.push_back({&item, info});
			}
			std::sort(items.begin(), items.end(), [](Item const &a, Item const &b){
				return a.info.lastModified() > b.info.lastModified();
			});
		}

		for (Item const &item : items) {
			QString s = misc::makeDateTimeString(item.info.lastModified());
			s = QString("[%1] %2").arg(s).arg(item.data->name);
			auto *child = NewItem(s, -1);
			tree->addTopLevelItem(child);
		}
	};

	// リポジトリリストを更新
	switch (style) {
	case RepositoryTreeWidget::RepositoryListStyle::_Keep:
		// nop
		break;
	case RepositoryTreeWidget::RepositoryListStyle::Standard:
		UpdateRepositoryListStandard(filter);
		break;
	case RepositoryTreeWidget::RepositoryListStyle::SortRecent:
		UpdateRepositoryListSortRecent(filter);
		break;
	default:
		Q_ASSERT(false);
		break;
	}
}

RepositoryTreeWidget::RepositoryListStyle RepositoryTreeWidget::currentRepositoryListStyle() const
{
	return current_repository_list_style_;
}

MainWindow *RepositoryTreeWidget::mainwindow()
{
	return qobject_cast<MainWindow *>(window());
}

void RepositoryTreeWidget::dragEnterEvent(QDragEnterEvent *event)
{
	if (QApplication::modalWindow()) return;

	if (event->mimeData()->hasUrls()) {
		event->acceptProposedAction();
		event->accept();
		return;
	}
	QTreeWidget::dragEnterEvent(event);
}

void RepositoryTreeWidget::dropEvent(QDropEvent *event)
{
	if (QApplication::modalWindow()) return;

	if (0) { // debug
		QMimeData const *mimedata = event->mimeData();
		QByteArray encoded = mimedata->data("application/x-qabstractitemmodeldatalist");
		QDataStream stream(&encoded, QIODevice::ReadOnly);
		while (!stream.atEnd()) {
			int row, col;
			QMap<int,  QVariant> roledatamap;
			stream >> row >> col >> roledatamap;
		}
	}

	if (event->mimeData()->hasUrls()) {
		Q_ASSERT(mainwindow());
		QStringList paths;

		QByteArray ba = event->mimeData()->data("text/uri-list");
		if (ba.size() > 4 && memcmp(ba.data(), "h\0t\0t\0p\0", 8) == 0) {
			QString path = QString::fromUtf16((char16_t const *)ba.data(), ba.size() / 2);
			int i = path.indexOf('\n');
			if (i >= 0) {
				path = path.mid(0, i);
			}
			if (!path.isEmpty()) {
				paths.push_back(path);
			}
		} else {
			QList<QUrl> urls = event->mimeData()->urls();
			for (QUrl const &url : urls) {
				QString path = url.url();
				paths.push_back(path);
			}
		}
		for (QString const &path : paths) {
			if (path.startsWith("file://")) {
				int i = 7;
#ifdef Q_OS_WIN
				if (path.utf16()[i] == '/') {
					i++;
				}
#endif
				mainwindow()->addExistingLocalRepository(path.mid(i), false);
			} else if (path.startsWith("https://github.com/")) {
				int i = 19;
				int j = path.indexOf('/', i);
				if (j > i) {
					QString username = path.mid(i, j - i);
					QString reponame = path.mid(j + 1);
				}
			}
		}
	} else {
		QTreeWidgetItem *item = current_item;
		QTreeWidget::dropEvent(event);
		setCurrentItem(item);
		emit dropped();
	}
}
