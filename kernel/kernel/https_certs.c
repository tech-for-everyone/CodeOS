/* https_certs.c -- embedded root CA store for mbedtls client certification.
 *
 * The PEM bundle below is a curated subset (16 roots) of the Mozilla CA
 * list, captured from the host's /usr/share/ca-certificates/mozilla/.
 * Roots are public certificates.
 *
 * Because CodeOS has no battery-backed clock, MBEDTLS_HAVE_TIME_DATE is not
 * defined, so mbedtls skips the certificate validity-date checks -- signature
 * chains, key usage and hostname are still fully verified.
 */
#include "https_certs.h"
#include "kprintf.h"
#include "string.h"

#include "mbedtls/build_info.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ssl.h"

extern void mbedtls_codeos_platform_init(void);

/* Embedded CA bundle (Mozilla CA list, curated subset). */
static const char s_ca_pem[] =
    "-----BEGIN CERTIFICATE-----\nMIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiw"
    "AwDQYJKoZIhvcNAQELBQAw\nTzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFN"
    "lY3VyaXR5IFJlc2Vh\ncmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUw"
    "NjA0MTEwNDM4\nWhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgS"
    "W50ZXJu\nZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdC"
    "BY\nMTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\nh"
    "77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n0TM8uk"
    "j13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\nA5/TR5d8mUg"
    "jU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\nT8KOEUt+zwvo/7V3"
    "LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\nB5T0Y3HsLuJvW5iB4YlcN"
    "Hlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\nB5iPNgiV5+I3lg02dZ77DnKxHZ"
    "u8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\nKBds0pjBqAlkd25HN7rOrFleaJ1/cta"
    "JxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\nOlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+"
    "e0ocS3MFEvzG6uBQE3xDk3SzynTn\njh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCz"
    "Lq9gwQbooMDQaHWBfEbwrbw\nqHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4h"
    "VC1CLQJ13hef4Y53CI\nrU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQ"
    "EAwIBBjAPBgNV\nHRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umb"
    "bjANBgkq\nhkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9"
    "lZL\nubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
    "3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\nNFtY2"
    "PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\nORAzI4JMPJ"
    "+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\nTkXWStAmzOVyygh"
    "qpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\njNPElpzVmbUq4JUagEiu"
    "TDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\noyi3B43njTOQ5yOf+1CceWxG1"
    "bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n4RgqsahDYVvTH9w7jXbyLeiNdd8XM2"
    "w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\nmRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubh"
    "PgXRR4Xq37Z0j4r7g1SgEEzwxA57d\nemyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b"
    "21/+jh5Xos1AnX5iItreGCc=\n-----END CERTIFICATE-----\n\n-----BEGIN CERTI"
    "FICATE-----\nMIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQ"
    "sFADBh\nMQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB"
    "3\nd3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\nMj"
    "AeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\nMRUwEwY"
    "DVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\nb20xIDAeBgNV"
    "BAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n9w0BAQEFAAOCAQ8AM"
    "IIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n2/Ou8jqJkTx65qsGGmvPrC"
    "3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n1x7e/dfgy5SDN67sH0NO3Xss0r0"
    "upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\nq2EGnI/yuum06ZIya7XzV+hdG82MHauV"
    "BJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\ntCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0"
    "xZKBgyMUNGPHgm+F6HmIcr9g+UQ\nvIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nM"
    "WxM4MphQIDAQABo0IwQDAP\nBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgN"
    "VHQ4EFgQUTiJUIBiV\n5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRv"
    "Dkhj6zHd6mcY\n1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuI"
    "EyExtv4\nNeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1"
    "NG\nFdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n8"
    "rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\npLiaWN"
    "0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\nMrY=\n-----"
    "END CERTIFICATE-----\n\n-----BEGIN CERTIFICATE-----\nMIICPzCCAcWgAwIBAg"
    "IQBVVWvPJepDU1w6QP1atFcjAKBggqhkjOPQQDAzBhMQsw\nCQYDVQQGEwJVUzEVMBMGA1U"
    "EChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3d3cu\nZGlnaWNlcnQuY29tMSAwHgYDVQQD"
    "ExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBHMzAe\nFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxM"
    "jAwMDBaMGExCzAJBgNVBAYTAlVTMRUw\nEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBA"
    "sTEHd3dy5kaWdpY2VydC5jb20x\nIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEc"
    "zMHYwEAYHKoZIzj0CAQYF\nK4EEACIDYgAE3afZu4q4C/sLfyHS8L6+c/MzXRq8NOrexpu8"
    "0JX28MzQC7phW1FG\nfp4tn+6OYwwX7Adw9c+ELkCDnOg/QW07rdOkFFk2eJ0DQ+4QE2xy3"
    "q6Ip6FrtUPO\nZ9wj/wMco+I+o0IwQDAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAw"
    "IBhjAd\nBgNVHQ4EFgQUs9tIpPmhxdiuNkHMEWNpYim8S8YwCgYIKoZIzj0EAwMDaAAwZQI"
    "x\nAK288mw/EkrRLTnDCgmXc/SINoyIJ7vmiI1Qhadj+Z4y3maTD/HMsQmP3Wyr+mt/\noA"
    "IwOWZbwmSNuJ5Q3KjVSaLtx9zRSX8XAbjIho9OjIgrqJqpisXRAL34VOKa5Vt8\nsycX\n-"
    "----END CERTIFICATE-----\n\n-----BEGIN CERTIFICATE-----\nMIIFkDCCA3igAw"
    "IBAgIQBZsbV56OITLiOQe9p3d1XDANBgkqhkiG9w0BAQwFADBi\nMQswCQYDVQQGEwJVUzE"
    "VMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\nd3cuZGlnaWNlcnQuY29tMSEw"
    "HwYDVQQDExhEaWdpQ2VydCBUcnVzdGVkIFJvb3Qg\nRzQwHhcNMTMwODAxMTIwMDAwWhcNM"
    "zgwMTE1MTIwMDAwWjBiMQswCQYDVQQGEwJV\nUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMR"
    "kwFwYDVQQLExB3d3cuZGlnaWNlcnQu\nY29tMSEwHwYDVQQDExhEaWdpQ2VydCBUcnVzdGV"
    "kIFJvb3QgRzQwggIiMA0GCSqG\nSIb3DQEBAQUAA4ICDwAwggIKAoICAQC/5pBzaN675F1K"
    "PDAiMGkz7MKnJS7JIT3y\nithZwuEppz1Yq3aaza57G4QNxDAf8xukOBbrVsaXbR2rsnnyy"
    "hHS5F/WBTxSD1If\nxp4VpX6+n6lXFllVcq9ok3DCsrp1mWpzMpTREEQQLt+C8weE5nQ7bX"
    "HiLQwb7iDV\nySAdYyktzuxeTsiT+CFhmzTrBcZe7FsavOvJz82sNEBfsXpm7nfISKhmV1e"
    "fVFiO\nDCu3T6cw2Vbuyntd463JT17lNecxy9qTXtyOj4DatpGYQJB5w3jHtrHEtWoYOAMQ"
    "\njdjUN6QuBX2I9YI+EJFwq1WCQTLX2wRzKm6RAXwhTNS8rhsDdV14Ztk6MUSaM0C/\nCNd"
    "aSaTC5qmgZ92kJ7yhTzm1EVgX9yRcRo9k98FpiHaYdj1ZXUJ2h4mXaXpI8OCi\nEhtmmnTK"
    "3kse5w5jrubU75KSOp493ADkRSWJtppEGSt+wJS00mFt6zPZxd9LBADM\nfRyVw4/3IbKyE"
    "be7f/LVjHAsQWCqsWMYRJUadmJ+9oCw++hkpjPRiQfhvbfmQ6QY\nuKZ3AeEPlAwhHbJUKS"
    "WJbOUOUlFHdL4mrLZBdd56rF+NP8m800ERElvlEFDrMcXK\nchYiCd98THU/Y+whX8QgUWt"
    "vsauGi0/C1kVfnSD8oR7FwI+isX4KJpn15GkvmB0t\n9dmpsh3lGwIDAQABo0IwQDAPBgNV"
    "HRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIB\nhjAdBgNVHQ4EFgQU7NfjgtJxXWRM3y5nP"
    "+e6mK4cD08wDQYJKoZIhvcNAQEMBQAD\nggIBALth2X2pbL4XxJEbw6GiAI3jZGgPVs93rn"
    "D5/ZpKmbnJeFwMDF/k5hQpVgs2\nSV1EY+CtnJYYZhsjDT156W1r1lT40jzBQ0CuHVD1Uvy"
    "QO7uYmWlrx8GnqGikJ9yd\n+SeuMIW59mdNOj6PWTkiU0TryF0Dyu1Qen1iIQqAyHNm0aAF"
    "YF/opbSnr6j3bTWc\nfFqK1qI4mfN4i/RN0iAL3gTujJtHgXINwBQy7zBZLq7gcfJW5GqXb"
    "5JQbZaNaHqa\nsjYUegbyJLkJEVDXCLG4iXqEI2FCKeWjzaIgQdfRnGTZ6iahixTXTBmyUE"
    "FxPT9N\ncCOGDErcgdLMMpSEDQgJlxxPwO5rIHQw0uA5NBCFIRUBCOhVMt5xSdkoF1BN5r5"
    "N\n0XWs0Mr7QbhDparTwwVETyw2m+L64kW4I1NsBm9nVX9GtUw/bihaeSbSpKhil9Ie\n4u"
    "1Ki7wb/UdKDd9nZn6yW0HQO+T0O/QEY+nvwlQAUaCKKsnOeMzV6ocEGLPOr0mI\nr/OSmba"
    "z5mEP0oUA51Aa5BuVnRmhuZyxm7EAHu/QD09CbMkKvO5D+jpxpchNJqU1\n/YldvIViHTLS"
    "oCtU7ZpXwdv6EM8Zt4tKG48BtieVU+i2iW1bvGjUI+iLUaJW+fCm\ngKDWHrO8Dw9TdSmq6"
    "hN35N6MgSGtBxBHEa2HPQfRdbzP82Z+\n-----END CERTIFICATE-----\n\n-----BEGI"
    "N CERTIFICATE-----\nMIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0"
    "BAQwFADBHMQsw\nCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz"
    "IExMQzEU\nMBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyM"
    "DAw\nMDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp\n"
    "Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA\nA4ICD"
    "wAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo\n27xqLpeXo4"
    "xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w\nCl7raKb0xlpHDU0"
    "QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw\nTcLCeoiKu7rPWRnWr4+w"
    "B7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl\nqAj+lug8aJRT7oM6iCsVlgmy4"
    "HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH\nszVsrBhQf4TgTM2S0yDpM7xSma8ytS"
    "mzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8\nY/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gH"
    "R6xYKu44LtcXFilWr06zqkUspzBmk\nMiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32J"
    "gfpMpf/VjsPOS+C12LOORc92\nwO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762"
    "DL8EGMUUXLeXdYWk70p\naDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEd"
    "rHbEg8QOB+DVrN\nVjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnq"
    "YNxgFlQID\nAQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNV"
    "HQ4E\nFgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb\n"
    "C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe\nQkEzC"
    "zc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy\nh6f88/qBVR"
    "RiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4\n7HLwEXWdyzRSjeZ"
    "2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J\nZgfIPxz88NtFMN9iiMG1"
    "D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef\nMgfdG+RCzgwARWGAtQsgWSl4v"
    "flVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/\nZ6tGn6D/Qqc6f1zLXbBwHSs09dR2CQ"
    "zreExZBfMzQsNhFRAbd03OIozUhfJFfbdT\n6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwL"
    "gjt7RETeJ9r/tSQdirpLsQBqvFAnZ\n0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXn"
    "s5lndcEZOitHTtNCjv0xyBZm\n2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeE"
    "MMA3KgqlbbC1j+Qa3bb\nbP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c\n-----END CER"
    "TIFICATE-----\n\n-----BEGIN CERTIFICATE-----\nMIIDQTCCAimgAwIBAgITBmyfz"
    "5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\nADA5MQswCQYDVQQGEwJVUzEPMA0GA1"
    "UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\nb24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDA"
    "wMFoXDTM4MDExNzAwMDAwMFowOTEL\nMAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZ"
    "MBcGA1UEAxMQQW1hem9uIFJv\nb3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCA"
    "QoCggEBALJ4gHHKeNXj\nca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNr"
    "oBvo3bSMgHFzZM\n9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNq"
    "cmzU5L/qw\nIFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmah"
    "RWa6\nVOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\njgSub"
    "JrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\nAYYwHQYDVR"
    "0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\nA4IBAQCY8jdaQZC"
    "hGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\nU5PMCCjjmCXPI6T53iHT"
    "fIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\nN+gDS63pYaACbvXy8MWy7Vu33"
    "PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\no/ufQJVtMVT8QtPHRh8jrdkPSHCa2X"
    "V4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtB"
    "V8ob2xJNDd2ZhwLnoQdeXeGADbkpy\nrqXRfboQnoZsG4q5WTP468SQvvG5\n-----END C"
    "ERTIFICATE-----\n\n-----BEGIN CERTIFICATE-----\nMIIFQTCCAymgAwIBAgITBmy"
    "f0pY1hp8KD+WGePhbJruKNzANBgkqhkiG9w0BAQwF\nADA5MQswCQYDVQQGEwJVUzEPMA0G"
    "A1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\nb24gUm9vdCBDQSAyMB4XDTE1MDUyNjAwM"
    "DAwMFoXDTQwMDUyNjAwMDAwMFowOTEL\nMAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbj"
    "EZMBcGA1UEAxMQQW1hem9uIFJv\nb3QgQ0EgMjCCAiIwDQYJKoZIhvcNAQEBBQADggIPADC"
    "CAgoCggIBAK2Wny2cSkxK\ngXlRmeyKy2tgURO8TW0G/LAIjd0ZEGrHJgw12MBvIITplLGb"
    "hQPDW9tK6Mj4kHbZ\nW0/jTOgGNk3Mmqw9DJArktQGGWCsN0R5hYGCrVo34A3MnaZMUnbqQ"
    "523BNFQ9lXg\n1dKmSYXpN+nKfq5clU1Imj+uIFptiJXZNLhSGkOQsL9sBbm2eLfq0OQ6PB"
    "JTYv9K\n8nu+NQWpEjTj82R0Yiw9AElaKP4yRLuH3WUnAnE72kr3H9rN9yFVkE8P7K6C4Z9"
    "r\n2UXTu/Bfh+08LDmG2j/e7HJV63mjrdvdfLC6HM783k81ds8P+HgfajZRRidhW+me\nz/"
    "CiVX18JYpvL7TFz4QuK/0NURBs+18bvBt+xa47mAExkv8LV/SasrlX6avvDXbR\n8O70zoa"
    "n4G7ptGmh32n2M8ZpLpcTnqWHsFcQgTfJU7O7f/aS0ZzQGPSSbtqDT6Zj\nmUyl+17vIWR6"
    "IF9sZIUVyzfpYgwLKhbcAS4y2j5L9Z469hdAlO+ekQiG+r5jqFoz\n7Mt0Q5X5bGlSNscpb"
    "/xVA1wf+5+9R+vnSUeVC06JIglJ4PVhHvG/LopyboBZ/1c6\n+XUyo05f7O0oYtlNc/LMgR"
    "dg7c3r3NunysV+Ar3yVAhU/bQtCSwXVEqY0VThUWcI\n0u1ufm8/0i2BWSlmy5A5lREedCf"
    "+3euvAgMBAAGjQjBAMA8GA1UdEwEB/wQFMAMB\nAf8wDgYDVR0PAQH/BAQDAgGGMB0GA1Ud"
    "DgQWBBSwDPBMMPQFWAJI/TPlUq9LhONm\nUjANBgkqhkiG9w0BAQwFAAOCAgEAqqiAjw54o"
    "+Ci1M3m9Zh6O+oAA7CXDpO8Wqj2\nLIxyh6mx/H9z/WNxeKWHWc8w4Q0QshNabYL1auaAn6"
    "AFC2jkR2vHat+2/XcycuUY\n+gn0oJMsXdKMdYV2ZZAMA3m3MSNjrXiDCYZohMr/+c8mmpJ"
    "5581LxedhpxfL86kS\nk5Nrp+gvU5LEYFiwzAJRGFuFjWJZY7attN6a+yb3ACfAXVU3dJnJ"
    "UH/jWS5E4ywl\n7uxMMne0nxrpS10gxdr9HIcWxkPo1LsmmkVwXqkLN1PiRnsn/eBG8om3z"
    "EK2yygm\nbtmlyTrIQRNg91CMFa6ybRoVGld45pIq2WWQgj9sAq+uEjonljYE1x2igGOpm/"
    "Hl\nurR8FLBOybEfdF849lHqm/osohHUqS0nGkWxr7JOcQ3AWEbWaQbLU8uz/mtBzUF+\nf"
    "UwPfHJ5elnNXkoOrJupmHN5fLT0zLm4BwyydFy4x2+IoZCn9Kr5v2c69BoVYh63\nn749sS"
    "mvZ6ES8lgQGVMDMBu4Gon2nL2XA46jCfMdiyHxtN/kHNGfZQIG6lzWE7OE\n76KlXIx3Kad"
    "owGuuQNKotOrN8I1LOJwZmhsoVLiJkO/KdYE+HvJkJMcYr07/R54H\n9jVlpNMKVv/1F2Rs"
    "76giJUmTtt8AF9pYfl3uxRuw0dFfIRDH+fO6AgonB8Xx1sfT\n4PsJYGw=\n-----END CE"
    "RTIFICATE-----\n\n-----BEGIN CERTIFICATE-----\nMIIBtjCCAVugAwIBAgITBmyf"
    "1XSXNmY/Owua2eiedgPySjAKBggqhkjOPQQDAjA5\nMQswCQYDVQQGEwJVUzEPMA0GA1UEC"
    "hMGQW1hem9uMRkwFwYDVQQDExBBbWF6b24g\nUm9vdCBDQSAzMB4XDTE1MDUyNjAwMDAwMF"
    "oXDTQwMDUyNjAwMDAwMFowOTELMAkG\nA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBc"
    "GA1UEAxMQQW1hem9uIFJvb3Qg\nQ0EgMzBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABCmX"
    "p8ZBf8ANm+gBG1bG8lKl\nui2yEujSLtf6ycXYqm0fc4E7O5hrOXwzpcVOho6AF2hiRVd9R"
    "FgdszflZwjrZt6j\nQjBAMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgGGMB0GA1"
    "UdDgQWBBSr\nttvXBp43rDCGB5Fwx5zEGbF4wDAKBggqhkjOPQQDAgNJADBGAiEA4IWSoxe"
    "3jfkr\nBqWTrBqYaGFy+uGh0PsceGCmQ5nFuMQCIQCcAu/xlJyzlvnrxir4tiz+OpAUFteM"
    "\nYyRIHN8wfdVoOw==\n-----END CERTIFICATE-----\n\n-----BEGIN CERTIFICATE"
    "-----\nMIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G"
    "\nA1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp\nZ24"
    "xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4\nMTAwMDAw"
    "WjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG\nA1UEChMKR2xvY"
    "mFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\nhvcNAQEBBQADggEPAD"
    "CCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8\nRgJDx7KKnQRfJMsuS+Fggkb"
    "hUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT\ngHeMCOFJ0mpiLx9e+pZo34knlTif"
    "Btc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm\nKPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yL"
    "y6c+9C7v/U9AOEGM+iCK65TpjoWc4zd\nQQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdb"
    "Kfd/+RFO+uIEn8rUAVSNECMWEZ\nXriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F"
    "2xmmFghcCAwEAAaNCMEAw\nDgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYD"
    "VR0OBBYEFI/wS3+o\nLkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAU"
    "Kr+yAzv95ZU\nRUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr2"
    "5sbwMp\njjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJC"
    "K\n6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX\nmc"
    "Ifeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs\nMx86OyX"
    "ShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH\nWD9f\n-----E"
    "ND CERTIFICATE-----\n\n-----BEGIN CERTIFICATE-----\nMIICOjCCAcGgAwIBAgI"
    "QQvLM2htpN0RfFf51KBC49DAKBggqhkjOPQQDAzBfMQsw\nCQYDVQQGEwJHQjEYMBYGA1UE"
    "ChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1T\nZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQ"
    "XV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcN\nMjEwMzIyMDAwMDAwWhcNNDYwMzIxMjM1OT"
    "U5WjBfMQswCQYDVQQGEwJHQjEYMBYG\nA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQ"
    "DEy1TZWN0aWdvIFB1YmxpYyBT\nZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQ"
    "BgcqhkjOPQIBBgUrgQQA\nIgNiAAR2+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw"
    "7LFeeyZYX8QeccC\nWvkEN/U0NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/i"
    "VStSBDHBv+\n6xnOQ6OjQjBAMB0GA1UdDgQWBBTRItpMWfFLXyY4qp3W7usNw/upYTAOBgN"
    "VHQ8B\nAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNnADBkAjAn7qRa"
    "\nqCG76UeXlImldCBteU/IvZNeWBj7LRoAasm4PdCkT0RHlAFWovgzJQxC36oCMB3q\n4S6"
    "ILuH5px0CMk7yn2xVdOOurvulGu7t0vzCAxHrRVxgED1cf5kDW21USAGKcw==\n-----END"
    " CERTIFICATE-----\n\n-----BEGIN CERTIFICATE-----\nMIIF3jCCA8agAwIBAgIQA"
    "f1tMPyjylGoG7xkDjUDLTANBgkqhkiG9w0BAQwFADCB\niDELMAkGA1UEBhMCVVMxEzARBg"
    "NVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0pl\ncnNleSBDaXR5MR4wHAYDVQQKExVUaGU"
    "gVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNV\nBAMTJVVTRVJUcnVzdCBSU0EgQ2VydGlmaWNh"
    "dGlvbiBBdXRob3JpdHkwHhcNMTAw\nMjAxMDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELM"
    "AkGA1UEBhMCVVMxEzARBgNV\nBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaX"
    "R5MR4wHAYDVQQKExVU\naGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnV"
    "zdCBSU0EgQ2Vy\ndGlmaWNhdGlvbiBBdXRob3JpdHkwggIiMA0GCSqGSIb3DQEBAQUAA4IC"
    "DwAwggIK\nAoICAQCAEmUXNg7D2wiz0KxXDXbtzSfTTK1Qg2HiqiBNCS1kCdzOiZ/MPans9"
    "s/B\n3PHTsdZ7NygRK0faOca8Ohm0X6a9fZ2jY0K2dvKpOyuR+OJv0OwWIJAJPuLodMkY\n"
    "tJHUYmTbf6MG8YgYapAiPLz+E/CHFHv25B+O1ORRxhFnRghRy4YUVD+8M/5+bJz/\nFp0Yv"
    "VGONaanZshyZ9shZrHUm3gDwFA66Mzw3LyeTP6vBZY1H1dat//O+T23LLb2\nVN3I5xI6Ta"
    "5MirdcmrS3ID3KfyI0rn47aGYBROcBTkZTmzNg95S+UzeQc0PzMsNT\n79uq/nROacdrjGC"
    "T3sTHDN/hMq7MkztReJVni+49Vv4M0GkPGw/zJSZrM233bkf6\nc0Plfg6lZrEpfDKEY1WJ"
    "xA3Bk1QwGROs0303p+tdOmw1XNtB1xLaqUkL39iAigmT\nYo61Zs8liM2EuLE/pDkP2QKe6"
    "xJMlXzzawWpXhaDzLhn4ugTncxbgtNMs+1b/97l\nc6wjOy0AvzVVdAlJ2ElYGn+SNuZRkg"
    "7zJn0cTRe8yexDJtC/QV9AqURE9JnnV4ee\nUB9XVKg+/XRjL7FQZQnmWEIuQxpMtPAlR1n"
    "6BB6T1CZGSlCBst6+eLf8ZxXhyVeE\nHg9j1uliutZfVS7qXMYoCAQlObgOK6nyTJccBz8N"
    "UvXt7y+CDwIDAQABo0IwQDAd\nBgNVHQ4EFgQUU3m/WqorSs9UgOHYm8Cd8rIDZsswDgYDV"
    "R0PAQH/BAQDAgEGMA8G\nA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEMBQADggIBAFzUfA"
    "3P9wF9QZllDHPF\nUp/L+M+ZBn8b2kMVn54CVVeWFPFSPCeHlCjtHzoBN6J2/FNQwISbxmt"
    "OuowhT6KO\nVWKR82kV2LyI48SqC/3vqOlLVSoGIG1VeCkZ7l8wXEskEVX/JJpuXior7gtN"
    "n3/3\nATiUFJVDBwn7YKnuHKsSjKCaXqeYalltiz8I+8jRRa8YFWSQEg9zKC7F4iRO/Fjs\n"
    "8PRF/iKz6y+O0tlFYQXBl2+odnKPi4w2r78NBc5xjeambx9spnFixdjQg3IM8WcR\niQycE"
    "0xyNN+81XHfqnHd4blsjDwSXWXavVcStkNr/+XeTWYRUc+ZruwXtuhxkYze\nSf7dNXGiFS"
    "eUHM9h4ya7b6NnJSFd5t0dCy5oGzuCr+yDZ4XUmFF0sbmZgIn/f3gZ\nXHlKYC6SQK5MNyo"
    "sycdiyA5d9zZbyuAlJQG03RoHnHcAP9Dc1ew91Pq7P8yF1m9/\nqS3fuQL39ZeatTXaw2ew"
    "h0qpKJ4jjv9cJ2vhsE/zB+4ALtRZh8tSQZXq9EfX7mRB\nVXyNWQKV3WKdwrnuWih0hKWbt"
    "5DHDAff9Yk2dDLWKMGwsAvgnEzDHNb842m1R0aB\nL6KCq9NjRHDEjf8tM7qtj3u1cIiuPh"
    "nPQCjY/MiQu12ZIvVS5ljFH4gxQ+6IHdfG\njjxDah2nGN59PRbxYvnKkKj9\n-----END "
    "CERTIFICATE-----\n\n-----BEGIN CERTIFICATE-----\nMIIDxTCCAq2gAwIBAgIBAD"
    "ANBgkqhkiG9w0BAQsFADCBgzELMAkGA1UEBhMCVVMx\nEDAOBgNVBAgTB0FyaXpvbmExEzA"
    "RBgNVBAcTClNjb3R0c2RhbGUxGjAYBgNVBAoT\nEUdvRGFkZHkuY29tLCBJbmMuMTEwLwYD"
    "VQQDEyhHbyBEYWRkeSBSb290IENlcnRp\nZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5M"
    "DkwMTAwMDAwMFoXDTM3MTIzMTIz\nNTk1OVowgYMxCzAJBgNVBAYTAlVTMRAwDgYDVQQIEw"
    "dBcml6b25hMRMwEQYDVQQH\nEwpTY290dHNkYWxlMRowGAYDVQQKExFHb0RhZGR5LmNvbSw"
    "gSW5jLjExMC8GA1UE\nAxMoR28gRGFkZHkgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkg"
    "LSBHMjCCASIw\nDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAL9xYgjx+lk09xvJGKP3g"
    "ElY6SKD\nE6bFIEMBO4Tx5oVJnyfq9oQbTqC023CYxzIBsQU+B07u9PpPL1kwIuerGVZr4o"
    "AH\n/PMWdYA5UXvl+TW2dE6pjYIT5LY/qQOD+qK+ihVqf94Lw7YZFAXK6sOoBJQ7Rnwy\nD"
    "fMAZiLIjWltNowRGLfTshxgtDj6AozO091GB94KPutdfMh8+7ArU6SSYmlRJQVh\nGkSBjC"
    "ypQ5Yj36w6gZoOKcUcqeldHraenjAKOc7xiID7S13MMuyFYkMlNAJWJwGR\ntDtwKj9usei"
    "ciAF9n9T521NtYJ2/LOdYq7hfRvzOxBsDPAnrSTFcaUaz4EcCAwEA\nAaNCMEAwDwYDVR0T"
    "AQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYE\nFDqahQcQZyi27/a9BUFuI"
    "MGU2g/eMA0GCSqGSIb3DQEBCwUAA4IBAQCZ21151fmX\nWWcDYfF+OwYxdS2hII5PZYe096"
    "acvNjpL9DbWu7PdIxztDhC2gV7+AJ1uP2lsdeu\n9tfeE8tTEH6KRtGX+rcuKxGrkLAngPn"
    "on1rpN5+r5N9ss4UXnT3ZJE95kTXWXwTr\ngIOrmgIttRD02JDHBHNA7XIloKmf7J6raBKZ"
    "V8aPEjoJpL1E/QYVN8Gb5DKj7Tjo\n2GTzLH4U/ALqn83/B2gX2yKQOC16jdFU8WnjXzPKe"
    "j17CuPKf1855eJ1usV2GDPO\nLPAvTK33sefOT6jEm0pUBsV/fdUID+Ic/n4XuKxe9tQWsk"
    "MJDE32p2u0mYRlynqI\n4uJEvlz36hz1\n-----END CERTIFICATE-----\n\n-----BEG"
    "IN CERTIFICATE-----\nMIIF2DCCA8CgAwIBAgIQTKr5yttjb+Af907YWwOGnTANBgkqhk"
    "iG9w0BAQwFADCB\nhTELMAkGA1UEBhMCR0IxGzAZBgNVBAgTEkdyZWF0ZXIgTWFuY2hlc3R"
    "lcjEQMA4G\nA1UEBxMHU2FsZm9yZDEaMBgGA1UEChMRQ09NT0RPIENBIExpbWl0ZWQxKzAp"
    "BgNV\nBAMTIkNPTU9ETyBSU0EgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMTE5\n"
    "MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBhTELMAkGA1UEBhMCR0IxGzAZBgNVBAgT\nEkdyZ"
    "WF0ZXIgTWFuY2hlc3RlcjEQMA4GA1UEBxMHU2FsZm9yZDEaMBgGA1UEChMR\nQ09NT0RPIE"
    "NBIExpbWl0ZWQxKzApBgNVBAMTIkNPTU9ETyBSU0EgQ2VydGlmaWNh\ndGlvbiBBdXRob3J"
    "pdHkwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQCR\n6FSS0gpWsawNJN3Fz0Rn"
    "dJkrN6N9I3AAcbxT38T6KhKPS38QVr2fcHK3YX/JSw8X\npz3jsARh7v8Rl8f0hj4K+j5c+"
    "ZPmNHrZFGvnnLOFoIJ6dq9xkNfs/Q36nGz637CC\n9BR++b7Epi9Pf5l/tfxnQ3K9DADWie"
    "trLNPtj5gcFKt+5eNu/Nio5JIk2kNrYrhV\n/erBvGy2i/MOjZrkm2xpmfh4SDBF1a3hDTx"
    "FYPwyllEnvGfDyi62a+pGx8cgoLEf\nZd5ICLqkTqnyg0Y3hOvozIFIQ2dOciqbXL1MGyiK"
    "XCJ7tKuY2e7gUYPDCUZObT6Z\n+pUX2nwzV0E8jVHtC7ZcryxjGt9XyD+86V3Em69FmeKjW"
    "iS0uqlWPc9vqv9JWL7w\nqP/0uK3pN/u6uPQLOvnoQ0IeidiEyxPx2bvhiWC4jChWrBQdnA"
    "rncevPDt09qZah\nSL0896+1DSJMwBGB7FY79tOi4lu3sgQiUpWAk2nojkxl8ZEDLXB0Auq"
    "LZxUpaVIC\nu9ffUGpVRr+goyhhf3DQw6KqLCGqR84onAZFdr+CGCe01a60y1Dma/RMhnEw"
    "6abf\nFobg2P9A3fvQQoh/ozM6LlweQRGBY84YcWsr7KaKtzFcOmpH4MN5WdYgGq/yapiq\n"
    "crxXStJLnbsQ/LBMQeXtHT1eKJ2czL+zUdqnR+WEUwIDAQABo0IwQDAdBgNVHQ4E\nFgQUu"
    "69+Aj36pvE8hI6t7jiY7NkyMtQwDgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB\n/wQFMAMBAf"
    "8wDQYJKoZIhvcNAQEMBQADggIBAArx1UaEt65Ru2yyTUEUAJNMnMvl\nwFTPoCWOAvn9sKI"
    "N9SCYPBMtrFaisNZ+EZLpLrqeLppysb0ZRGxhNaKatBYSaVqM\n4dc+pBroLwP0rmEdEBsq"
    "pIt6xf4FpuHA1sj+nq6PK7o9mfjYcwlYRm6mnPTXJ9OV\n2jeDchzTc+CiR5kDOF3VSXkAK"
    "RzH7JsgHAckaVd4sjn8OoSgtZx8jb8uk2Intzna\nFxiuvTwJaP+EmzzV1gsD41eeFPfR60"
    "/IvYcjt7ZJQ3mFXLrrkguhxuhoqEwWsRqZ\nCuhTLJK7oQkYdQxlqHvLI7cawiiFwxv/0Ct"
    "i76R7CZGYZ4wUAc1oBmpjIXUDgIiK\nboHGhfKppC3n9KUkEEeDys30jXlYsQab5xoq2Z0B"
    "15R97QNKyvDb6KkBPvVWmcke\njkk9u+UJueBPSZI9FoJAzMxZxuY67RIuaTxslbH9qh17f"
    "4a+Hg4yRvv7E491f0yL\nS0Zj/gA0QHDBw7mh3aZw4gSzQbzpgJHqZJx64SIDqZxubw5lT2"
    "yHh17zbqD5daWb\nQOhTsiedSrnAdyGN/4fy3ryM7xfft0kL0fJuMAsaDk527RH89elWsn2"
    "/x20Kk4yl\n0MC2Hb46TpSi125sC8KKfPog88Tk5c0NqMuRkrF8hey1FGlmDoLnzc7ILaZR"
    "fyHB\nNVOFBkpdn627G190\n-----END CERTIFICATE-----\n\n-----BEGIN CERTIFI"
    "CATE-----\nMIICCTCCAY6gAwIBAgINAgPluILrIPglJ209ZjAKBggqhkjOPQQDAzBHMQsw"
    "CQYD\nVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n"
    "A1UEAxMLR1RTIFJvb3QgUjMwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\nWjBHM"
    "QswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\nIExMQzEUMB"
    "IGA1UEAxMLR1RTIFJvb3QgUjMwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\nAAQfTzOHMymKoYT"
    "ey8chWEGJ6ladK0uFxh1MJ7x/JlFyb+Kf1qPKzEUURout736G\njOyxfi//qXGdGIRFBEFV"
    "bivqJn+7kAHjSxm65FSWRQmx1WyRRK2EE46ajA2ADDL2\n4CejQjBAMA4GA1UdDwEB/wQEA"
    "wIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\nBBTB8Sa6oC2uhYHP0/EqEr24Cmf9vD"
    "AKBggqhkjOPQQDAwNpADBmAjEA9uEglRR7\nVKOQFhG/hMjqb2sXnh5GmCCbn9MN2azTL81"
    "8+FsuVbu/3ZL3pAzcMeGiAjEA/Jdm\nZuVDFhOD3cffL74UOO0BzrEXGhF16b0DjyZ+hOXJ"
    "YKaV11RZt+cRLInUue4X\n-----END CERTIFICATE-----\n\n-----BEGIN CERTIFICA"
    "TE-----\nMIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQ"
    "YD\nVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\nA"
    "1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\nWjBHMQ"
    "swCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\nIExMQzEUMBI"
    "GA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\nAATzdHOnaItgrkO4"
    "NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi\nQHY7qca4R9gq55KRanPps"
    "XI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR\nHYqjQjBAMA4GA1UdDwEB/wQEAw"
    "IBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\nBBSATNbrdP9JNqPV2Py1PsVq8JQdjDA"
    "KBggqhkjOPQQDAwNpADBmAjEA6ED/g94D\n9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8de"
    "Vl5c1RxYIigL9zC2L7F8AjEA8GE8\np/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaV"
    "sjh4rsUecrNIdSUtUlD\n-----END CERTIFICATE-----\n\n-----BEGIN CERTIFICAT"
    "E-----\nMIIFijCCA3KgAwIBAgIQdY39i658BwD6qSWn4cetFDANBgkqhkiG9w0BAQwFADB"
    "f\nMQswCQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQD\nEy"
    "1TZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBSNDYw\nHhcNMjE"
    "wMzIyMDAwMDAwWhcNNDYwMzIxMjM1OTU5WjBfMQswCQYDVQQGEwJHQjEY\nMBYGA1UEChMP"
    "U2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1Ymxp\nYyBTZXJ2ZXIgQXV0a"
    "GVudGljYXRpb24gUm9vdCBSNDYwggIiMA0GCSqGSIb3DQEB\nAQUAA4ICDwAwggIKAoICAQ"
    "CTvtU2UnXYASOgHEdCSe5jtrch/cSV1UgrJnwUUxDa\nef0rty2k1Cz66jLdScK5vQ9IPXt"
    "amFSvnl0xdE8H/FAh3aTPaE8bEmNtJZlMKpnz\nSDBh+oF8HqcIStw+KxwfGExxqjWMrfhu"
    "6DtK2eWUAtaJhBOqbchPM8xQljeSM9xf\niOefVNlI8JhD1mb9nxc4Q8UBUQvX4yMPFF1bF"
    "OdLvt30yNoDN9HWOaEhUTCDsG3X\nME6WW5HwcCSrv0WBZEMNvSE6Lzzpng3LILVCJ8zab5"
    "vuZDCQOc2TZYEhMbUjUDM3\nIuM47fgxMMxF/mL50V0yeUKH32rMVhlATc6qu/m1dkmU8Sf"
    "4kaWD5QazYw6A3OAS\nVYCmO2a0OYctyPDQ0RTp5A1NDvZdV3LFOxxHVp3i1fuBYYzMTYCQ"
    "NFu31xR13NgE\nSJ/AwSiItOkcyqex8Va3e0lMWeUgFaiEAin6OJRpmkkGj80feRQXEgyDe"
    "t4fsZfu\n+Zd4KKTIRJLpfSYFplhym3kT2BFfrsU4YjRosoYwjviQYZ4ybPUHNs2iTG7sij"
    "bt\n8uaZFURww3y8nDnAtOFr94MlI1fZEoDlSfB1D++N6xybVCi0ITz8fAr/73trdf+L\nH"
    "aAZBav6+CuBQug4urv7qv094PPK306Xlynt8xhW6aWWrL3DkJiy4Pmi1KZHQ3xt\nzwIDAQ"
    "ABo0IwQDAdBgNVHQ4EFgQUVnNYZJX5khqwEioEYnmhQBWIIUkwDgYDVR0P\nAQH/BAQDAgG"
    "GMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEMBQADggIBAC9c\nmTz8Bl6MlC5w6tIy"
    "MY208FHVvArzZJ8HXtXBc2hkeqK5Duj5XYUtqDdFqij0lgVQ\nYKlJfp/imTYpE0RHap1VI"
    "DzYm/EDMrraQKFz6oOht0SmDpkBm+S8f74TlH7Kph52\ngDY9hAaLMyZlbcp+nv4fjFg4ex"
    "qDsQ+8FxG75gbMY/qB8oFM2gsQa6H61SilzwZA\nFv97fRheORKkU55+MkIQpiGRqRxOF3y"
    "EvJ+M0ejf5lG5Nkc/kLnHvALcWxxPDkjB\nJYOcCj+esQMzEhonrPcibCTRAUH4WAP+JWgi"
    "H5paPHxsnnVI84HxZmduTILA7rpX\nDhjvLpr3Etiga+kFpaHpaPi8TD8SHkXoUsCjvxIne"
    "bnMMTzD9joiFgOgyY9mpFui\nTdaBJQbpdqQACj7LzTWb4OE4y2BThihCQRxEV+ioratF4y"
    "UQvNs+ZUH7G6aXD+u5\ndHn5HrwdVw1Hr8Mvn4dGp+smWg9WY7ViYG4A++MnESLn/pmPNPW"
    "56MORcr3Ywx65\nLvKRRFHQV80MNNVIIb/bE/FmJUNS0nAiNs2fxBx1IK1jcmMGDw4nztJq"
    "Dby1ORrp\n0XZ60Vzk50lJLVU3aPAaOpg+VBeHVOmmJ1CJeyAvP/+/oYtKR5j/K3tJPsMpR"
    "mAY\nQqszKbrAKbkTidOIijlBO8n9pu0f9GBj39ItVQGL\n-----END CERTIFICATE----"
    "-\n";
;
static struct mbedtls_x509_crt s_ca_chain;
static int s_ca_up = 0;      /* 1 = store parsed and ready */
static char s_pem_buf[4096]; /* NUL-terminated PEM block handed to mbedtls */
static int s_insecure = 0;   /* dev escape hatch */

const char *https_ca_pem(void) {
    return s_ca_pem;
}

int https_set_insecure(int enabled) {
    s_insecure = enabled ? 1 : 0;
    return s_insecure;
}

int https_insecure(void) {
    return s_insecure;
}

int https_ca_init(void) {
    if (s_ca_up) return 0;
    mbedtls_codeos_platform_init();
    mbedtls_x509_crt_init(&s_ca_chain);

    /*
     * Parse each PEM certificate individually so a single unsupported or
     * invalid root never takes down the whole store.  A trust store with a
     * few unparsable roots is still perfectly usable (real-world behavior).
     */
    static const char TAG_BEGIN[]   = "-----BEGIN CERTIFICATE-----";
    static const char TAG_BEGIN_T[] = "-----BEGIN TRUSTED CERTIFICATE-----";
    static const char TAG_END[]     = "-----END CERTIFICATE-----";
    const char *b = s_ca_pem;
    const char *end = s_ca_pem + sizeof(s_ca_pem) - 1;
    int ok = 0, skipped = 0, idx = 0;

    while (b + sizeof(TAG_BEGIN) - 1 <= end ||
           b + sizeof(TAG_BEGIN_T) - 1 <= end) {
        const char *begin = NULL;
        int blen = 0;
        {   /* prefer plain CERTIFICATE marker, then TRUSTED variant */
            const char *p = b;
            while (p + (sizeof(TAG_BEGIN) - 1) <= end) {
                if (strncmp(p, TAG_BEGIN, sizeof(TAG_BEGIN) - 1) == 0) {
                    begin = p; blen = (int)(sizeof(TAG_BEGIN) - 1); break;
                }
                p++;
            }
            if (!begin) {
                p = b;
                while (p + (sizeof(TAG_BEGIN_T) - 1) <= end) {
                    if (strncmp(p, TAG_BEGIN_T, sizeof(TAG_BEGIN_T) - 1) == 0) {
                        begin = p; blen = (int)(sizeof(TAG_BEGIN_T) - 1); break;
                    }
                    p++;
                }
            }
        }
        if (!begin) break;

        const char *close = begin + blen;
        while (close + (sizeof(TAG_END) - 1) <= end) {
            if (strncmp(close, TAG_END, sizeof(TAG_END) - 1) == 0) break;
            close++;
        }
        if (close + (sizeof(TAG_END) - 1) > end) {
            kprintf("https: CA bundle truncated at cert #%d\n", idx);
            break;
        }
        close += sizeof(TAG_END) - 1;

        idx++;
        size_t clen = (size_t)(close - begin);
        int ret = -1;
        if (clen + 1 <= sizeof(s_pem_buf)) {
            memcpy(s_pem_buf, begin, clen);
            s_pem_buf[clen] = '\0';   /* mbedtls requires a trailing NUL to
                                       * recognize a PEM (vs DER) buffer */
            ret = mbedtls_x509_crt_parse(&s_ca_chain,
                                         (const unsigned char *)s_pem_buf,
                                         clen + 1);
        } else {
            kprintf("https: CA store cert #%d too big (%d)\n", idx, (int)clen);
        }
        if (ret != 0) {
            skipped++;
            kprintf("https: CA store skipped root #%d (mbedtls 0x%04x)\n",
                    idx, (unsigned)(-ret));
        } else {
            ok++;
        }
        b = close;
    }
    if (ok == 0) {
        kprintf("https: CA store parse FAILED (0 roots accepted)\n");
        mbedtls_x509_crt_free(&s_ca_chain);
        return -1;
    }
    s_ca_up = 1;
    kprintf("https: CA store ready (%d roots, %d skipped)\n", ok, skipped);
    return 0;
}

struct mbedtls_x509_crt *https_ca_get(void) {
    if (!s_ca_up && https_ca_init() != 0) return NULL;
    return &s_ca_chain;
}

int https_trust_ca_pem(const char *pem, size_t len) {
    if (!pem || len == 0) return -1;
    struct mbedtls_x509_crt *chain = https_ca_get();
    if (!chain) return -1;
    if (mbedtls_x509_crt_parse(chain, (const unsigned char *)pem, len) != 0)
        return -1;
    return 0;
}
