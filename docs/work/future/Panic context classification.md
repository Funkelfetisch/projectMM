# Panic context classification

The ESP32 panic recorder runs before the SDK finalizes abort classification. A captured FAULT value can therefore represent a synthetic task-watchdog context rather than a hardware exception. On the HELIO bench, the saved System boot reason TASK_WDT qualifies the reset with record `n=2/image=f5cfdd9e`; its raw EXCCAUSE value `0x3fcca29c` does not establish stack corruption.

The pinned SDK task-watchdog path sets its reset hint and abort flags, obtains the interrupted task's saved context and enters the Xtensa exception path. The real panic handler subsequently changes the category to ABORT. The recorder currently reads the saved context's exception fields without distinguishing that origin.

A correction replaces unqualified cause output with a validity-qualified numeric context:

- Capture the SDK abort/task-watchdog indicators, reset hint and pseudo-cause available in the qualified SDK version, together with the original category.
- Record whether hardware exception fields are valid. Unknown values remain unknown rather than becoming zero or an apparent exception cause.
- Use the correct exception or solicited task-frame layout. The minimal safe fallback retains the supplied program counter and omits additional manual unwinding.
- Version the retained record and its integrity check. Classification must survive a later software restart without borrowing that later boot's reset reason.

SDK-fixture tests must distinguish a genuine exception, synthetic task-watchdog and abort contexts, invalid frame bounds, incomplete records and later software restarts. Physical acceptance requires the retained classification to agree with an independently observed reset reason. No raw strings, stack contents or keys are added.

This recorder correction is unimplemented. The separate HTTP liveness changes require their own device acceptance; this classification issue proves neither defective hardware nor their physical success.
