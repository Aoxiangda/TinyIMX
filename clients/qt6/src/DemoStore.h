#pragma once
#include <QAbstractListModel>
#include <QObject>
#include <QVariantMap>
#include <QHash>

// An in-memory design fixture. Never connects to, authenticates with or writes to a server.
class RowModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    explicit RowModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    const QList<QVariantMap> &rows() const { return rows_; }
    void replace(QList<QVariantMap> rows);
    void append(const QVariantMap &row);
    void update(int index, const QVariantMap &fields);
    Q_INVOKABLE QVariantMap get(int index) const;
signals:
    void countChanged();
private:
    QList<QVariantMap> rows_;
};

class DemoStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(RowModel* conversations READ conversations CONSTANT)
    Q_PROPERTY(RowModel* messages READ messages CONSTANT)
    Q_PROPERTY(RowModel* contacts READ contacts CONSTANT)
    Q_PROPERTY(RowModel* requests READ requests CONSTANT)
    Q_PROPERTY(RowModel* groups READ groups CONSTANT)
    Q_PROPERTY(RowModel* transfers READ transfers CONSTANT)
    Q_PROPERTY(QVariantMap current READ current NOTIFY selectionChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)
    Q_PROPERTY(bool online READ online WRITE setOnline NOTIFY onlineChanged)
    Q_PROPERTY(bool groupMuted READ groupMuted NOTIFY groupChanged)
    Q_PROPERTY(QString aiState READ aiState NOTIFY aiChanged)
    Q_PROPERTY(QString aiAnswer READ aiAnswer NOTIFY aiChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
public:
    explicit DemoStore(QObject *parent = nullptr);
    RowModel *conversations() { return &conversations_; }
    RowModel *messages() { return &messages_; }
    RowModel *contacts() { return &contacts_; }
    RowModel *requests() { return &requests_; }
    RowModel *groups() { return &groups_; }
    RowModel *transfers() { return &transfers_; }
    QVariantMap current() const;
    int selectedIndex() const { return selected_; }
    QString draft() const { return drafts_.value(selected_); }
    void setDraft(const QString &value);
    bool online() const { return online_; }
    bool groupMuted() const { return selected_ == 0 && groupMuted_; }
    QString aiState() const { return aiState_; }
    QString aiAnswer() const { return aiAnswer_; }
    QString notice() const { return notice_; }
    void setOnline(bool value);
    Q_INVOKABLE void selectConversation(int index);
    Q_INVOKABLE bool sendMessage(const QString &text);
    Q_INVOKABLE void retryMessage(int index);
    Q_INVOKABLE void markRead();
    Q_INVOKABLE void acceptRequest(int index, bool accept);
    Q_INVOKABLE void addFriend(const QString &identity, const QString &note);
    Q_INVOKABLE void createGroup(const QString &name, const QString &description);
    Q_INVOKABLE void groupAction(const QString &action);
    Q_INVOKABLE void transferAction(int index, const QString &action);
    Q_INVOKABLE void chooseFile();
    Q_INVOKABLE void askAI(const QString &prompt);
    Q_INVOKABLE void cancelAI();
    Q_INVOKABLE void notify(const QString &text);
    Q_INVOKABLE void loadEarlier();
    int runSelfTest(const QString &reportPath = {});
signals:
    void selectionChanged();
    void draftChanged();
    void onlineChanged();
    void groupChanged();
    void aiChanged();
    void noticeChanged();
private:
    RowModel conversations_, messages_, contacts_, requests_, groups_, transfers_;
    QHash<int, QList<QVariantMap>> history_;
    QHash<int, QString> drafts_;
    int selected_{0};
    bool online_{true}, groupMuted_{false};
    QString aiState_{QStringLiteral("idle")}, aiAnswer_, notice_;
    int aiGeneration_{0};
    void seed();
    void snapshot();
};
