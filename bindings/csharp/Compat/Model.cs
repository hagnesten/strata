using System;
using System.Collections.Generic;
using Microsoft.EntityFrameworkCore;

namespace Strata.Compat {

	public sealed class Account {

		public long Id { get; set; }
		public string Name { get; set; }
		public decimal Cash { get; set; }
		public List < Order > Orders { get; set; } = new List < Order > ( );

	}

	public sealed class Order {

		public long Id { get; set; }
		public long AccountId { get; set; }
		public Account Account { get; set; }
		public string Symbol { get; set; }
		public string Side { get; set; }
		public int Quantity { get; set; }
		public decimal LimitPrice { get; set; }
		public int Filled { get; set; }
		public string Status { get; set; }
		public DateTime Placed { get; set; }
		public Guid ClientId { get; set; }
		public bool Urgent { get; set; }
		public List < Fill > Fills { get; set; } = new List < Fill > ( );

	}

	public sealed class Fill {

		public long Id { get; set; }
		public long OrderId { get; set; }
		public Order Order { get; set; }
		public int Quantity { get; set; }
		public decimal Price { get; set; }
		public DateTime FilledAt { get; set; }

	}

	public sealed class TradeContext : DbContext {

		private readonly string connection;

		public TradeContext ( string connection ) {

			this.connection = connection;

		}

		public DbSet < Account > Accounts { get; set; }
		public DbSet < Order > Orders { get; set; }
		public DbSet < Fill > Fills { get; set; }

		protected override void OnConfiguring ( DbContextOptionsBuilder options ) {

			options.UseNpgsql ( connection );

		}

		protected override void OnModelCreating ( ModelBuilder model ) {

			model.HasDefaultSchema ( "efcheck" );
			model.Entity < Account > ( ).Property ( a => a.Name ).HasMaxLength ( 64 ).IsRequired ( );
			model.Entity < Account > ( ).Property ( a => a.Cash ).HasPrecision ( 20, 4 );
			model.Entity < Order > ( ).Property ( o => o.Symbol ).HasMaxLength ( 16 ).IsRequired ( );
			model.Entity < Order > ( ).Property ( o => o.Side ).HasMaxLength ( 1 ).IsRequired ( );
			model.Entity < Order > ( ).Property ( o => o.Status ).HasMaxLength ( 16 ).IsRequired ( );
			model.Entity < Order > ( ).Property ( o => o.LimitPrice ).HasPrecision ( 18, 4 );
			model.Entity < Order > ( ).HasIndex ( o => new { o.Symbol, o.Status } );
			model.Entity < Order > ( ).HasIndex ( o => o.ClientId ).IsUnique ( );
			model.Entity < Fill > ( ).Property ( f => f.Price ).HasPrecision ( 18, 4 );

		}

	}

}
