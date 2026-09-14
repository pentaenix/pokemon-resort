"""Small paintable capacity-mask editor."""

from __future__ import annotations

from PySide6.QtWidgets import QGridLayout, QPushButton, QSizePolicy, QWidget


class CapacityMaskWidget(QWidget):
    MAX_COLUMNS = 6
    MAX_ROWS = 4

    def __init__(self, parent=None):
        super().__init__(parent)
        self._cells = [[False] * self.MAX_COLUMNS for _ in range(self.MAX_ROWS)]
        self._buttons: list[list[QPushButton]] = []
        layout = QGridLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(3)
        for row in range(self.MAX_ROWS):
            button_row = []
            for column in range(self.MAX_COLUMNS):
                button = QPushButton()
                button.setCheckable(True)
                button.setFixedSize(34, 34)
                button.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
                button.setToolTip("Toggle this capacity cell")
                button.toggled.connect(
                    lambda checked, r=row, c=column: self._set_cell(r, c, checked)
                )
                layout.addWidget(button, row, column)
                button_row.append(button)
            self._buttons.append(button_row)
        self.set_mask(["1"])

    def _set_cell(self, row: int, column: int, checked: bool) -> None:
        self._cells[row][column] = checked

    def set_mask(self, rows: list[str]) -> None:
        self._cells = [[False] * self.MAX_COLUMNS for _ in range(self.MAX_ROWS)]
        for row, value in enumerate(rows[:self.MAX_ROWS]):
            for column, marker in enumerate(value[:self.MAX_COLUMNS]):
                self._cells[row][column] = marker == "1"
        if not any(any(row) for row in self._cells):
            self._cells[0][0] = True
        for row in range(self.MAX_ROWS):
            for column in range(self.MAX_COLUMNS):
                self._buttons[row][column].setChecked(self._cells[row][column])

    def mask(self) -> list[str]:
        active = [(row, col) for row in range(self.MAX_ROWS)
                  for col in range(self.MAX_COLUMNS) if self._cells[row][col]]
        if not active:
            return []
        max_row = max(row for row, _col in active)
        max_col = max(col for _row, col in active)
        return [
            "".join("1" if self._cells[row][column] else "0"
                    for column in range(max_col + 1))
            for row in range(max_row + 1)
        ]

