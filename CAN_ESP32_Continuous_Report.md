# Polling CAN với ESP32 và LED đỏ

Ngày: 2026-10-16.

## Luồng mới trong main.c

Thêm profile `CAN_TEST_PROFILE_ESP32_CONTINUOUS=2`. Giữ profile 0 (loopback) làm mặc định và profile 1 (bài test STM32 gửi một lần). Không sửa CAN wrapper, board CAN initialization, RTD, generated configuration, clock initialization hoặc `.cproject`.

Trong profile 2:

- CAN0 Normal, Classic CAN, Standard ID, TX MB0/RX MB1, polling hoàn toàn qua wrapper hiện có.
- Gửi lặp lại Standard ID **0x123**, DLC **8**, data **11 22 33 44 55 66 77 88**; khoảng cách tối thiểu theo bộ đếm khoảng **100 ms** giữa các yêu cầu được chấp nhận. Frame đầu gửi ngay sau khi init/start thành công.
- Khi TX còn pending chỉ gọi PollTx; không ghi đè, hủy hoặc gửi thêm. Chỉ gửi tiếp sau khi PollTx xác nhận hoàn tất và đủ khoảng gửi. Nếu frame trước chậm hoàn tất thì chu kỳ thực tế dài hơn 100 ms.
- PollRx ở mọi vòng lặp, độc lập với trạng thái TX. Không có iteration budget kết thúc ứng dụng.
- Mặc định nhận Standard ID **0x321**, cho phép DLC 0..8 và payload bất kỳ hợp lệ. Không yêu cầu dữ liệu mẫu của bài test STM32. Có thể đặt build symbol `CAN_APP_RX_ID` để khớp ESP32. Driver hiện có lọc đúng một Standard ID; không tự mở nhận mọi ID và không hỗ trợ Extended trong profile này.
- Mỗi đợt RX kích hoạt LED đỏ sáng khoảng **200 ms**, rồi tắt ít nhất khoảng **200 ms**. LED service không chặn polling CAN. Các frame tới dồn trong lúc LED đang bận được gộp thành một yêu cầu nháy tiếp; bộ đếm RX vẫn đếm từng frame được driver giao.
- Frame RX overrun vẫn có dữ liệu hợp lệ theo hợp đồng wrapper: lưu frame, nháy LED và tăng bộ đếm overrun. Frame không hợp lệ tăng bộ đếm riêng và không kích hoạt LED.
- API error hoặc bus-off giữ diagnostics, chuyển FAULT và vào `CanTest_HoldResults`. Không tự Stop/Deinit/cancel/resend. CAN controller có thể còn tự retry frame pending; reset có chủ đích trước lần thử mới.

Người dùng đã xác nhận trong cuộc trò chuyện rằng đã kiểm tra tương thích mức logic S32K144–VP230 và cách ly RX của SBC khỏi PTE4. Đây là xác nhận của người dùng, không phải phép đo do phần mềm thực hiện. Chưa có thông tin ID/bitrate từ ESP32, nên phần triển khai giữ giả định Standard 0x321 / 500 kbit/s; cần chỉnh ID nếu ESP32 khác. Transceiver ngoài trên ESP32 không tự quyết định CAN ID hoặc bitrate.

## LED và định thời

NXP ghi nhận RGB đỏ tại PTD15 trong [hướng dẫn S32K144EVB](https://www.nxp.com/document/guide/getting-started-with-the-s32k144evb%3ANGS-S32K144EVB). [Schematic Rev C, D11/R97 và RGB_RED](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/S32K/8896/1/SPF-29248_C.pdf) là cơ sở cho LED đấu về VDD: kéo PTD15 xuống thấp để sáng. Cần phân biệt LED RGB ứng dụng với LED báo OpenSDA/reset.

Chỉ profile 2 cấu hình PTD15 GPIO ALT1, kiểm tra PCC PORTD và PCR lock, preload mức OFF trước khi bật output, xử lý ISF W1C và kiểm tra MUX readback. Không thay đổi PTE4/PTE5 ngoài `BoardCan_InitPins()` hiện có; không thay đổi các chân LED khác.

Định thời dùng SysTick 24-bit free-running theo `Clock_Ip_GetClockFrequency(CORE_CLK)`, đọc giá trị CVR bằng polling. TICKINT=0; không có SysTick handler hay CAN IRQ mới. Cấu hình hiện tại có `OSIF_USE_SYSTEM_TIMER=STD_OFF`; profile từ chối build nếu OSIF system timer được bật và từ chối chạy nếu SysTick đã enable hoặc bật interrupt. Không sửa RTD/OSIF internals và không dùng OSIF_COUNTER_DUMMY làm mốc milliseconds.

Đây là định thời từ clock do RTD báo, chưa đo trên board. Modular delta chỉ xử lý được khi mỗi vòng polling ngắn hơn một vòng tràn SysTick: khoảng **350 ms tại core 48 MHz**. Debugger halt dài hoặc service bị chặn lâu có thể làm mất số vòng tràn và kéo dài thời gian LED/TX; không coi đây là deadline thời gian thực. Không bật register watch liên tục lên CSR/ESR1. Không dùng profile này cùng một chủ sở hữu SysTick khác.

## Debugger

Profile 2 sử dụng **`g_can_app`**, không dùng PASS/FAIL một lần của `g_can_test`:

| Trường | Ý nghĩa |
|---|---|
| `state` | 0 NOT_STARTED, 1 RUNNING, 2 FAULT, 3 HARDWARE_BLOCKED |
| `stage` / `failure_stage` | 1 gate, 2 CAN board pins, 3 LED/timer, 4 Init, 5 Start, 6 polling |
| `tx_accepted_count` / `tx_count` | Số yêu cầu chấp nhận / số TX xác nhận hoàn tất |
| `tx_pending` | Frame hiện tại chưa được PollTx xác nhận hoàn tất |
| `rx_count`, `received` | Số frame nhận được và frame mới nhất |
| `rx_overrun_count`, `rx_invalid_count` | Chẩn đoán RX mất frame / frame không hỗ trợ |
| `led_on`, `led_blinks_started` | LED đang sáng / số lần bắt đầu nháy |
| `core_clock_hz`, `tx_period_ticks`, `led_period_ticks` | Clock và ngưỡng định thời được sử dụng |
| `diagnostics`, `failure_diagnostics` | Cache wrapper hiện tại và snapshot lúc lỗi đầu tiên |
| `first_failure_status` | API status đầu tiên gây dừng |

Các bộ đếm frame/LED bão hòa tại UINT32_MAX. Khi đang chạy bình thường, ứng dụng không vào HoldResults và không kết thúc sau frame RX đầu tiên. Có thể Suspend để xem biến rồi Resume; không step từng vòng khi đánh giá tốc độ truyền.

## Thiết lập S32DS do người dùng thực hiện

Không chạy build/debug bằng command trong phiên này.

1. F5 Refresh project. Vào **Properties > C/C++ Build > Settings > Tool Settings > Standard S32DS C Compiler > Preprocessor / Defined symbols**.
2. Để chạy bài ESP32, đặt **`CAN_TEST_PROFILE=2`** và **`CAN_TEST_HARDWARE_VERIFIED=1`** sau xác nhận phần cứng nêu trên. Mặc định trong source vẫn là profile 0 / gate 0; không tự chuyển cấu hình IDE.
3. Nếu ESP32 gửi ID khác, thêm `CAN_APP_RX_ID=0x...` (Standard 0..0x7FF). Cả hai node cần cấu hình Classic CAN 500 kbit/s, non-silent để ACK; xác minh common GND, CANH/CANL và termination.
4. Dùng **Project > Build Project**, gửi Build Console hoặc lỗi đầu tiên để cùng kiểm tra. Dùng `-g3`/`-O0` qua UI nếu cần xem biến, và chọn ELF mới trong debug launch.
5. Chạy ESP32 nhận trước, rồi Resume S32K144. ESP32 nên thấy ID 0x123 lặp lại. Cho ESP32 gửi Standard ID 0x321 hoặc ID đã đặt: RGB đỏ sẽ nháy, `rx_count` tăng. Không cần payload A1 B2… của bài test cũ.
6. Nếu không gửi được, xem `state`, `failure_stage`, API status và diagnostics cache, không chỉ dựa vào LED. Chỉ có RX từ ID được lọc mới kích hoạt LED. Không ACK có thể giữ TX pending; không ghi đè frame đó.
7. Để quay về loopback regression, đổi `CAN_TEST_PROFILE=0` và gate 0 rồi build lại; bài test STM32 cũ vẫn ở profile 1.

## Kiểm tra và giới hạn

Thêm host simulation cho profile 2, kiểm tra TX pacing/ownership, nhận payload tùy ý, LED không dùng IRQ, bảo toàn pending TX khi lỗi và từ chối SysTick đã có chủ. Các fake header được mở rộng riêng cho host, không nằm trên target include path. Không thêm module CAN mới.

Tôi chưa chạy build/host tests/debug và chưa có phép thử ESP32/LED trên board. Kiểm tra tĩnh xác nhận clock block cũ không đổi, mặc định source vẫn loopback/gate 0, TX qua wrapper có pending guard và các SysTick mask khớp header đang cài. PowerShell runner parse không lỗi; `git diff --check` không báo lỗi whitespace.

Trong lúc làm việc, `.cproject` và các output Debug_FLASH/ELF thay đổi từ phía ngoài các thao tác chỉnh source của tôi; tôi không sửa hoặc hoàn tác chúng. Cấu hình UI đọc ở cuối phiên đang là **CAN_TEST_PROFILE=1**, hardware gate=1. ELF vừa được cập nhật không xác minh nhánh ESP32 profile 2. Cần người dùng đổi profile=2, build qua UI và cung cấp kết quả để xác nhận build mới. Không tuyên bố thành công phần cứng.
