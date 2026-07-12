package com.limvario.companion;

import android.app.Activity;
import android.graphics.Color;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.WifiNetworkSpecifier;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

/**
 * Coquille native L!M Vario.
 *
 *  - Se connecte au WiFi du vario (SSID "LIM-Vario") via WifiNetworkSpecifier
 *    (Android 10+), puis LIE le trafic de l'app a ce reseau (bindProcessToNetwork)
 *    -> resout le probleme "Android route par la 4G" : 192.168.4.1 reste joignable.
 *  - Affiche l'app companion existante (servie par le vario) dans une WebView.
 *  - Si pas connecte : ecran de repli avec bouton "Se connecter au vario".
 */
public class MainActivity extends Activity {

    private static final String SSID    = "LIM-Vario";
    private static final String PASS    = "limvario";
    private static final String APP_URL = "http://192.168.4.1/";

    private ConnectivityManager cm;
    private ConnectivityManager.NetworkCallback cb;
    private WebView web;
    private boolean pageLoaded = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        cm = (ConnectivityManager) getSystemService(CONNECTIVITY_SERVICE);
        showStatus("Connexion au vario…", false);
        requestVarioNetwork();
    }

    /** Demande la connexion au WiFi du vario et lie le trafic de l'app dessus. */
    private void requestVarioNetwork() {
        WifiNetworkSpecifier specifier = new WifiNetworkSpecifier.Builder()
                .setSsid(SSID)
                .setWpa2Passphrase(PASS)
                .build();

        NetworkRequest request = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .setNetworkSpecifier(specifier)
                .build();

        if (cb != null) {
            try { cm.unregisterNetworkCallback(cb); } catch (Exception ignored) {}
        }
        cb = new ConnectivityManager.NetworkCallback() {
            @Override public void onAvailable(Network network) {
                // Force tout le trafic de l'app a passer par le WiFi du vario.
                cm.bindProcessToNetwork(network);
                runOnUiThread(MainActivity.this::showApp);
            }
            @Override public void onUnavailable() {
                runOnUiThread(() -> showStatus(
                        "Impossible de se connecter au vario.\n"
                        + "Vérifie qu'il est allumé et que « App connect » est activé.", true));
            }
            @Override public void onLost(Network network) {
                pageLoaded = false;
                runOnUiThread(() -> showStatus(
                        "Connexion au vario perdue.", true));
            }
        };
        // 30 s pour approuver la boite de dialogue systeme, sinon onUnavailable.
        cm.requestNetwork(request, cb, 30000);
    }

    /** Affiche la WebView pointee sur l'app du vario. */
    private void showApp() {
        if (web == null) {
            web = new WebView(this);
            WebSettings s = web.getSettings();
            s.setJavaScriptEnabled(true);
            s.setDomStorageEnabled(true);
            web.setWebViewClient(new WebViewClient() {
                @Override
                public void onReceivedError(WebView view, WebResourceRequest req, WebResourceError err) {
                    if (req != null && req.isForMainFrame()) {
                        showStatus("L'application du vario ne répond pas.", true);
                    }
                }
            });
        }
        setContentView(web);
        if (!pageLoaded) {
            web.loadUrl(APP_URL);
            pageLoaded = true;
        }
    }

    /** Ecran de statut / non connecte (construit en code, pas de layout XML). */
    private void showStatus(String message, boolean showButton) {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(Color.parseColor("#0f172a"));
        int pad = dp(24);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("L!M Vario");
        title.setTextColor(Color.WHITE);
        title.setTextSize(28);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        if (!showButton) {
            ProgressBar spin = new ProgressBar(this);
            LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            sp.topMargin = pad;
            spin.setLayoutParams(sp);
            root.addView(spin);
        }

        TextView msg = new TextView(this);
        msg.setText(message);
        msg.setTextColor(Color.parseColor("#94a3b8"));
        msg.setTextSize(15);
        msg.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams mp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        mp.topMargin = pad;
        msg.setLayoutParams(mp);
        root.addView(msg);

        if (showButton) {
            Button btn = new Button(this);
            btn.setText("Se connecter au vario");
            btn.setOnClickListener(v -> {
                showStatus("Connexion au vario…", false);
                requestVarioNetwork();
            });
            LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            bp.topMargin = dp(28);
            btn.setLayoutParams(bp);
            root.addView(btn);
        }

        setContentView(root);
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density);
    }

    @Override
    public void onBackPressed() {
        if (web != null && web.getParent() != null && web.canGoBack()) {
            web.goBack();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (cb != null) {
            try { cm.unregisterNetworkCallback(cb); } catch (Exception ignored) {}
        }
        // Relache la liaison reseau pour ne pas bloquer le trafic des autres apps.
        cm.bindProcessToNetwork(null);
    }
}
