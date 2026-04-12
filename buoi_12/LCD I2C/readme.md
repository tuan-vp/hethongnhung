# Lưu ý
Đã bổ sung thêm hàm để tạo ký tự tùy chỉnh (custom character) vào CGRAM:

```c
void CLCD_I2C_CreateChar(CLCD_I2C_Name* LCD, uint8_t location, uint8_t charmap[]);
