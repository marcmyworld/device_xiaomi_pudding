# File permissions and capabilities

# Thales StrongBox Keymint service
[odm/bin/hw/android.hardware.security.keymint-service.strongbox-thales]
mode: 0755
user: AID_VENDOR_THALES_STRONGBOX
group: AID_SYSTEM
caps: SYS_ADMIN SYS_NICE WAKE_ALARM

# Weaver service
[odm/bin/hw/android.hardware.weaver-service.thales]
mode: 0755
user: AID_VENDOR_THALES_WEAVER
group: AID_SYSTEM
caps: SYS_ADMIN SYS_NICE WAKE_ALARM
