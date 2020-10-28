#include "listeninterfacedialog.hpp"

#include <sstream>
#if defined _WIN64 || defined _WIN32
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

#include <WS2tcpip.h>
#include <iphlpapi.h>

#include <wx/hyperlink.h>
#include <wx/statbmp.h>
#include <wx/sizer.h>

#include "../clientdata.hpp"
#include "../translator.hpp"
#include "../../core/utils.hpp"

struct NetworkAdapter
{
    std::string name;
    std::string address;
};

using pt::UI::Dialogs::ListenInterfaceDialog;

ListenInterfaceDialog::ListenInterfaceDialog(wxWindow* parent, wxWindowID id, std::string address, int port)
    : wxDialog(parent, id, wxEmptyString)
{
    auto grid = new wxFlexGridSizer(2, FromDIP(5), FromDIP(5));
    grid->AddGrowableCol(1, 1);

    m_adapters = new wxChoice(this, wxID_ANY);
    m_port = new wxTextCtrl(this, wxID_ANY);

    grid->Add(new wxStaticText(this, wxID_ANY, i18n("network_adapter")));
    grid->Add(m_adapters, 1, wxEXPAND | wxALL);
    grid->Add(new wxStaticText(this, wxID_ANY, i18n("port")));
    grid->Add(m_port);

    auto ok = new wxButton(this, wxID_OK, i18n("ok"));

    auto buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->AddStretchSpacer();
    buttonSizer->Add(ok);
    buttonSizer->Add(new wxButton(this, wxID_CANCEL, i18n("cancel")));

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 1, wxEXPAND | wxALL, FromDIP(10));
    sizer->Add(buttonSizer, 0, wxEXPAND | wxBOTTOM | wxRIGHT, FromDIP(10));

    this->SetSizerAndFit(sizer);
    this->SetTitle(i18n("listen_interface"));

    wxSize size{ FromDIP(380), this->GetSize().GetHeight() };
    this->SetSize(size);

    this->LoadAdapters();

    if (address.size() > 0)
    {
        for (unsigned int i = 0; i < m_adapters->GetCount(); i++)
        {
            auto na = static_cast<ClientData<NetworkAdapter>*>(m_adapters->GetClientObject(i));

            if (na->GetValue().address == address)
            {
                m_adapters->SetSelection(i);
                break;
            }
        }
    }

    if (m_adapters->GetSelection() < 0)
    {
        m_adapters->SetSelection(0);
    }

    if (port > 0)
    {
        m_port->SetValue(std::to_string(port));
    }

    ok->Enable(false);

    m_adapters->Bind(
        wxEVT_CHOICE,
        [this, ok](wxCommandEvent&)
        {
            ok->Enable(true);
        });

    m_port->Bind(
        wxEVT_TEXT,
        [this, ok](wxCommandEvent&)
        {
            ok->Enable(!m_port->GetValue().IsEmpty());
        });
}

ListenInterfaceDialog::~ListenInterfaceDialog()
{
}

std::string ListenInterfaceDialog::GetAddress()
{
    auto na = static_cast<ClientData<NetworkAdapter>*>(m_adapters->GetClientObject(m_adapters->GetSelection()));
    return na->GetValue().address;
}

int ListenInterfaceDialog::GetPort()
{
    return std::atoi(m_port->GetValue());
}

void ListenInterfaceDialog::LoadAdapters()
{
    NetworkAdapter any4;
    any4.address = "0.0.0.0";
    any4.name = "0.0.0.0";

    NetworkAdapter any6;
    any6.address = "[::]";
    any6.name = "[::]";

    m_adapters->Insert(
        any4.name,
        m_adapters->GetCount(),
        new ClientData<NetworkAdapter>(any4));

    m_adapters->Insert(
        any6.name,
        m_adapters->GetCount(),
        new ClientData<NetworkAdapter>(any6));

    static constexpr int MAX_GETADAPTERSADDRESSES_TRIES = 10;
    static constexpr int INITIAL_BUFFER_SIZE = 15000;

    ULONG len = INITIAL_BUFFER_SIZE;
    ULONG flags = 0;
    char initial_buf[INITIAL_BUFFER_SIZE];
    std::unique_ptr<char[]> buf;

    IP_ADAPTER_ADDRESSES* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(&initial_buf);
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &len);

    for (int tries = 1; result == ERROR_BUFFER_OVERFLOW && tries < MAX_GETADAPTERSADDRESSES_TRIES; ++tries)
    {
        buf.reset(new char[len]);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.get());
        result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &len);
    }

    if (result == NO_ERROR)
    {
        for (const IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != NULL; adapter = adapter->Next)
        {
            // Ignore the loopback device.
            if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            {
                continue;
            }

            if (adapter->OperStatus != IfOperStatusUp)
            {
                continue;
            }

            for (IP_ADAPTER_UNICAST_ADDRESS* address = adapter->FirstUnicastAddress; address; address = address->Next)
            {
                int family = address->Address.lpSockaddr->sa_family;

                switch (family)
                {
                case AF_INET:
                {
                    char addrbuf[INET_ADDRSTRLEN] = {};
                    sockaddr_in* si = reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr);
                    inet_ntop(AF_INET, &(si->sin_addr), addrbuf, sizeof(addrbuf));

                    std::stringstream ss;
                    ss << addrbuf << " (" << Utils::toStdString(adapter->FriendlyName) << ")";

                    NetworkAdapter ipv4;
                    ipv4.address = addrbuf;
                    ipv4.name = ss.str();

                    m_adapters->Insert(
                        ipv4.name,
                        m_adapters->GetCount(),
                        new ClientData<NetworkAdapter>(ipv4));

                    break;
                }
                case AF_INET6:
                {
                    char addrbuf[INET6_ADDRSTRLEN] = {};
                    sockaddr_in6* si = reinterpret_cast<sockaddr_in6*>(address->Address.lpSockaddr);
                    inet_ntop(AF_INET6, &(si->sin6_addr), addrbuf, sizeof(addrbuf));

                    std::stringstream ss;
                    ss << "[" << addrbuf << "]" << " (" << Utils::toStdString(adapter->FriendlyName) << ")";

                    NetworkAdapter ipv6;
                    ipv6.address = "[" + std::string(addrbuf) + "]";
                    ipv6.name = ss.str();

                    m_adapters->Insert(
                        ipv6.name,
                        m_adapters->GetCount(),
                        new ClientData<NetworkAdapter>(ipv6));

                    break;
                }
                }
            }
        }
    }
}
