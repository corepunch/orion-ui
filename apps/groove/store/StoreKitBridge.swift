import StoreKit
#if os(macOS)
import AppKit
#else
import UIKit
#endif

@MainActor
private final class GrooveStoreKit {
    static let shared = GrooveStoreKit()

    private let ids: [String] = {
        var ids: [String] = []
        while let id = groove_store_product_id(Int32(ids.count)) {
            ids.append(String(cString: id))
        }
        return ids
    }()
    private var products: [String: Product] = [:]
    private var listener: Task<Void, Never>?
    private var pending: Set<String> = []
    private var revision = 0

    func start() {
        guard listener == nil else { return }
        listener = Task {
            for await update in Transaction.updates {
                guard case .verified(let transaction) = update,
                      ids.contains(transaction.productID) else { continue }
                await refreshEntitlements()
                await transaction.finish()
                if pending.remove(transaction.productID) != nil, transaction.revocationDate == nil {
                    groove_store_purchase_resolved()
                }
            }
        }
        Task { await refreshEntitlements() }
        Task { await load() }
    }

    private func refreshEntitlements() async {
        revision += 1
        let request = revision
        var owned: UInt32 = 0
        for await result in Transaction.currentEntitlements {
            if case .verified(let transaction) = result,
               let index = ids.firstIndex(of: transaction.productID) {
                owned |= 1 << index
            }
        }
        if request == revision {
            groove_store_set_entitlements(owned)
        }
    }

    func load() async {
        groove_store_set_status(GR_STORE_LOADING)
        do {
            products = Dictionary(uniqueKeysWithValues: try await Product.products(for: ids)
                .filter { $0.type == .nonConsumable }.map { ($0.id, $0) })
        } catch {
            products = [:]
        }
        for (index, id) in ids.enumerated() {
            (products[id]?.displayPrice ?? "").withCString { groove_store_set_price(Int32(index), $0) }
        }
        groove_store_set_status(products.count == ids.count ? GR_STORE_READY : GR_STORE_UNAVAILABLE)
    }

    private func confirm(_ product: Product) async throws -> Product.PurchaseResult {
        #if os(macOS)
        if #available(macOS 15.2, *), let window = NSApp.keyWindow ?? NSApp.mainWindow {
            return try await product.purchase(confirmIn: window)
        }
        #else
        if #available(iOS 17.0, *),
           let scene = UIApplication.shared.connectedScenes.first(where: { $0.activationState == .foregroundActive }) {
            return try await product.purchase(confirmIn: scene)
        }
        #endif
        return try await product.purchase()
    }

    func buy(_ index: Int) async {
        guard ids.indices.contains(index), let product = products[ids[index]] else {
            groove_store_set_status(GR_STORE_PURCHASE_FAILED)
            return
        }
        guard !pending.contains(product.id) else {
            groove_store_set_status(GR_STORE_PENDING)
            return
        }
        do {
            switch try await confirm(product) {
            case .success(.verified(let transaction)):
                await refreshEntitlements()
                await transaction.finish()
                groove_store_set_status(GR_STORE_PURCHASED)
            case .pending:
                pending.insert(product.id)
                groove_store_set_status(GR_STORE_PENDING)
            case .userCancelled:
                groove_store_set_status(GR_STORE_READY)
            case .success(.unverified(_, _)):
                groove_store_set_status(GR_STORE_PURCHASE_FAILED)
            @unknown default:
                groove_store_set_status(GR_STORE_PURCHASE_FAILED)
            }
        } catch {
            groove_store_set_status(GR_STORE_PURCHASE_FAILED)
        }
    }

    func restore() async {
        do {
            try await AppStore.sync()
            await refreshEntitlements()
            if products.isEmpty { await load() }
            groove_store_set_status(GR_STORE_RESTORED)
        } catch {
            groove_store_set_status(GR_STORE_RESTORE_FAILED)
        }
    }
}

@_cdecl("groove_storekit_start")
func grooveStoreKitStart() { Task { @MainActor in GrooveStoreKit.shared.start() } }

@_cdecl("groove_storekit_load")
func grooveStoreKitLoad() { Task { @MainActor in await GrooveStoreKit.shared.load() } }

@_cdecl("groove_storekit_buy")
func grooveStoreKitBuy(_ index: Int32) { Task { @MainActor in await GrooveStoreKit.shared.buy(Int(index)) } }

@_cdecl("groove_storekit_restore")
func grooveStoreKitRestore() { Task { @MainActor in await GrooveStoreKit.shared.restore() } }
