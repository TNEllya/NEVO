"""User list layout debug script."""
import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from PyQt5.QtWidgets import QApplication, QHBoxLayout, QWidget
from PyQt5.QtCore import Qt, QTimer
from qfluentwidgets import HeaderCardWidget
from views.user_list import UserListView


def main():
    app = QApplication(sys.argv)

    card = HeaderCardWidget("Users")
    ulv = UserListView()

    print(f"Before update_users: ulv visible={ulv.isVisible()}")

    ulv.update_users([
        {"id": 1, "username": "Alice"},
        {"id": 2, "username": "Bob", "group_id": 1},
        {"id": 3, "username": "Charlie", "muted": True},
    ], local_user_id=1, is_admin=False, channel_name="Lobby")

    print(f"After update_users: ulv visible={ulv.isVisible()}")
    for i in range(ulv._rows_layout.count()):
        item = ulv._rows_layout.itemAt(i)
        if item.widget():
            w = item.widget()
            print(f"  row[{i}] visible={w.isVisible()} geo={w.geometry().width()}x{w.geometry().height()}")

    card.view.layout().addWidget(ulv)
    print(f"After addWidget: ulv visible={ulv.isVisible()}")

    w = QWidget()
    w.setLayout(QHBoxLayout())
    w.layout().addWidget(card)
    w.resize(400, 300)
    w.show()

    def dump():
        print(f"\n=== After show() ===")
        print(f"ulv visible={ulv.isVisible()} geo={ulv.geometry().width()}x{ulv.geometry().height()}")
        print(f"card.view geo={card.view.geometry().width()}x{card.view.geometry().height()}")
        for i in range(ulv._rows_layout.count()):
            item = ulv._rows_layout.itemAt(i)
            if item.widget():
                w2 = item.widget()
                print(f"  row[{i}] visible={w2.isVisible()} geo={w2.geometry().width()}x{w2.geometry().height()}")
        app.quit()

    QTimer.singleShot(500, dump)
    app.exec_()


if __name__ == "__main__":
    main()
